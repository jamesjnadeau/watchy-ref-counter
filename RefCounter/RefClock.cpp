#include "RefClock.h"

#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>

#include "RefBleTime.h"
#include "RefSyncSchedule.h"
#include "RefZone.h"
#include "board.h"
#include "settings.h"

namespace {

// All four survive deep sleep, so low power mode neither restarts the resync
// schedule nor resets the uptime shown in the menu. lastActivity is the
// anchor an automatic resync counts its quiet period from -- it must live
// here too, or every deep-sleep wake would look like a fresh idle watch.
RTC_DATA_ATTR time_t lastSyncAttempt = 0;
RTC_DATA_ATTR time_t lastSyncOk      = 0;
RTC_DATA_ATTR time_t bootedAt        = 0;
RTC_DATA_ATTR time_t lastActivity    = 0;

// Which of the two sources last set the clock. Survives deep sleep with the
// stamps it belongs to, so the About screen still says "BT" after a night in
// low power mode.
RTC_DATA_ATTR uint8_t lastSyncKind = RefClock::SYNC_NONE;

// Unlike the above, this must NOT survive deep sleep: it limits a WiFi-less
// watch to one sync attempt per wake cycle, rather than spinning on an unset
// RTC every time syncDue() is polled.
bool unsetSyncTried = false;

// How often a Bluetooth sync window checks whether a phone has written yet.
// Only the automatic path uses this; the menu polls on its own button
// interval so it can watch for BACK at the same time.
const uint32_t BLE_POLL_MS = 50;

// Shared by syncDue() and secondsUntilSyncDue() so that one RTC read serves
// both. Reading the chip costs two I2C transactions and a struct tm round
// trip, and the idle loop asks whether a sync is due several times a second,
// so the second read is worth avoiding.
uint32_t secondsUntilDueAt(time_t now) {
  return RefSyncSchedule::secondsUntilDue(now, lastActivity, lastSyncAttempt,
                                          NTP_RESYNC_HOURS,
                                          NTP_MIN_SYNC_INTERVAL_HOURS);
}

} // namespace

void RefClock::begin(bool coldBoot) {
  // The RTC chip, and everything built on top of it, works in UTC. Keeping the
  // C library in UTC too means mktime and localtime_r round-trip a struct tm
  // unchanged, so RefRtc never has to think about zones. The local time the
  // watch displays is produced in one place, by localNow() below.
  setenv("TZ", "UTC0", 1);
  tzset();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  _rtc.begin();
  RefZone::begin();

  if (coldBoot) {
    bootedAt = _rtc.epoch();
  }

  // Give a first boot an anchor to count the quiet period from. Only when it
  // is still 0, though: restamping on every wake -- including the deep-sleep
  // wake this schedule itself arms -- would push the quiet period out
  // forever and the resync would never fire.
  if (lastActivity == 0) {
    lastActivity = _rtc.epoch();
  }
}

bool RefClock::localNow(struct tm &out) {
  const time_t utc = _rtc.epoch();
  if (utc == 0) {
    return false;
  }
  const time_t local = utc + RefZone::offsetSecondsAt(utc);
  // gmtime_r rather than localtime_r: the offset has already been folded in,
  // and applying it twice is exactly the bug this arrangement avoids.
  gmtime_r(&local, &out);
  return true;
}

bool RefClock::setLocal(const struct tm &local) {
  struct tm copy = local;
  copy.tm_isdst  = 0;
  const time_t asTyped = mktime(&copy); // TZ is UTC0, so this is a plain count
  if (asTyped == (time_t)-1) {
    return false;
  }
  const time_t utc = asTyped - RefZone::offsetSecondsForLocal(asTyped);

  struct tm utcTm;
  gmtime_r(&utc, &utcTm);
  return _rtc.set(utcTm);
}

long RefClock::utcOffsetSeconds() {
  return RefZone::offsetSecondsAt(_rtc.epoch());
}

const char *RefClock::zoneAbbrev() { return RefZone::abbrevAt(_rtc.epoch()); }

bool RefClock::read(uint8_t &hour, uint8_t &minute) {
  struct tm t;
  if (!localNow(t)) {
    return false;
  }
  hour   = (uint8_t)t.tm_hour;
  minute = (uint8_t)t.tm_min;
  return true;
}

bool RefClock::syncFromNtp() {
  // Zero offset: what goes into the RTC is UTC. The zone is applied on the way
  // out, in localNow(), so a zone or daylight saving change needs no re-sync.
  configTime(0, 0, NTP_SERVER);
  // configTime writes TZ itself, from that offset. It lands on "UTC0", which
  // is what this project wants anyway, but re-assert it rather than depend on
  // how a given core version spells it.
  setenv("TZ", "UTC0", 1);
  tzset();

  struct tm fetched;
  const bool ok = getLocalTime(&fetched, NTP_TIMEOUT_MS);
  if (ok) {
    _rtc.set(fetched);
  }

  // configTime leaves the SNTP service running, and it keeps its own poll
  // timer going long after the radio is off. Nothing here wants a background
  // task re-setting the clock behind the RTC's back, so stop it.
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  esp_sntp_stop();
#else
  sntp_stop(); // pre-IDF5 spelling, for older Arduino cores
#endif

  lastSyncAttempt = _rtc.epoch();
  if (ok) {
    lastSyncOk   = lastSyncAttempt;
    lastSyncKind = SYNC_NTP;
  }
  return ok;
}

bool RefClock::applyBleStamp(const RefCtsTime::Stamp &stamp) {
  bool ok = false;

  if (stamp.haveOffset) {
    // The phone sent the zone its clock is in, so the conversion is exact and
    // needs nothing from RefZone. TZ is UTC0 for the life of the sketch, so
    // mktime here is a plain count of seconds, not a local time conversion.
    struct tm copy = stamp.wall;
    copy.tm_isdst  = 0;
    const time_t asWritten = mktime(&copy);
    if (asWritten != (time_t)-1) {
      const time_t utc = asWritten - stamp.offsetSeconds;
      struct tm utcTm;
      gmtime_r(&utc, &utcTm);
      ok = _rtc.set(utcTm);
    }
  } else {
    // No zone from the phone: treat what it wrote as local to the zone the
    // watch is set to. That is the same assumption the Set Time screen makes,
    // and setLocal applies the daylight saving rule with it.
    ok = setLocal(stamp.wall);
  }

  // Stamped after the write, so the attempt is recorded against the newly set
  // clock rather than the drifted one it replaced.
  lastSyncAttempt = _rtc.epoch();
  if (ok) {
    lastSyncOk   = lastSyncAttempt;
    lastSyncKind = SYNC_BT;
  }
  return ok;
}

void RefClock::publishBleTime(const struct tm &local) {
  RefBleTime::publish(local, utcOffsetSeconds());
}

void RefClock::noteSyncAttempt() { lastSyncAttempt = _rtc.epoch(); }

bool RefClock::syncFromBluetooth(uint32_t windowMs) {
  if (!RefBleTime::begin()) {
    noteSyncAttempt();
    return false;
  }

  // Let a phone that reads the characteristics see the watch's current time
  // rather than an empty value. Skipped when the clock has never been set,
  // which is the case where there is nothing truthful to publish.
  struct tm local;
  if (localNow(local)) {
    publishBleTime(local);
  }

  bool ok = false;
  RefCtsTime::Stamp stamp;
  RefCtsTime::clear(stamp);
  const uint32_t deadline = millis() + windowMs;
  while ((int32_t)(millis() - deadline) < 0) {
    if (RefBleTime::take(stamp)) {
      ok = applyBleStamp(stamp);
      break;
    }
    delay(BLE_POLL_MS);
  }

  RefBleTime::end();
  if (!ok) {
    noteSyncAttempt();
  }
  return ok;
}

bool RefClock::connectAndSync() {
  bool ok = false;
  // No arguments means "use the credentials saved by the WiFi setup screen".
  if (WiFi.begin() != WL_CONNECT_FAILED &&
      WiFi.waitForConnectResult() == WL_CONNECTED) {
    ok = syncFromNtp();
  }
  // Off before Bluetooth comes up, not just at the end: the two radios share
  // one antenna and the memory the BLE controller wants is memory the WiFi
  // stack is holding.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Fall back to a phone only if NTP produced nothing and the fallback is
  // switched on. It is off by default: unlike NTP, this one needs an app on a
  // phone that syncs time by itself, and advertising to nobody costs battery.
  if (!ok && BT_AUTO_SYNC_SECONDS > 0 && RefBleTime::supported()) {
    ok = syncFromBluetooth((uint32_t)BT_AUTO_SYNC_SECONDS * 1000UL);
  }

  if (!ok) {
    noteSyncAttempt();
  }
  return ok;
}

void RefClock::noteActivity() {
  // A failed RTC read comes back as 0, and to the schedule a 0 anchor is not
  // a missing one -- it is an activity stamp from 1970, which any quiet
  // period is long past. Letting one glitched read through would therefore
  // make a sync fall due at once, radio and all, in the middle of a game, so
  // an unreadable clock leaves the last good stamp where it is.
  const time_t t = _rtc.epoch();
  if (t != 0) {
    lastActivity = t;
  }
}

bool RefClock::syncDue() {
  if (NTP_RESYNC_HOURS == 0) {
    return false;
  }
  const time_t now = _rtc.epoch();
  if (now == 0) {
    // An unset RTC has no NTP source but WiFi, so this is the only way it
    // ever gets a time. Try once per wake cycle rather than every time the
    // sketch happens to poll, since a WiFi-less watch would otherwise spin.
    if (unsetSyncTried) {
      return false;
    }
    unsetSyncTried = true;
    return true;
  }
  return secondsUntilDueAt(now) == 0;
}

uint32_t RefClock::secondsUntilSyncDue() {
  return secondsUntilDueAt(_rtc.epoch());
}

float RefClock::batteryVolts() {
  return (analogReadMilliVolts(PIN_BATT_ADC) / 1000.0f) * BATT_DIVIDER;
}

time_t RefClock::bootEpoch() const { return bootedAt; }
time_t RefClock::lastSyncEpoch() const { return lastSyncOk; }

RefClock::SyncSource RefClock::lastSyncSource() const {
  return (SyncSource)lastSyncKind;
}
