#include "RefClock.h"

#include <WiFi.h>
#include <Wire.h>
#include <esp_chip_info.h>
#include <esp_sntp.h>

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

// Unlike the above, this must NOT survive deep sleep: it limits a WiFi-less
// watch to one sync attempt per wake cycle, rather than spinning on an unset
// RTC every time syncDue() is polled.
bool unsetSyncTried = false;

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
    lastSyncOk = lastSyncAttempt;
  }
  return ok;
}

bool RefClock::connectAndSync() {
  bool ok = false;
  // No arguments means "use the credentials saved by the WiFi setup screen".
  if (WiFi.begin() != WL_CONNECT_FAILED &&
      WiFi.waitForConnectResult() == WL_CONNECTED) {
    ok = syncFromNtp();
  } else {
    lastSyncAttempt = _rtc.epoch();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return ok;
}

void RefClock::noteActivity() { lastActivity = _rtc.epoch(); }

bool RefClock::syncDue() {
  if (NTP_RESYNC_HOURS == 0) {
    return false;
  }
  if (_rtc.epoch() == 0) {
    // An unset RTC has no NTP source but WiFi, so this is the only way it
    // ever gets a time. Try once per wake cycle rather than every time the
    // sketch happens to poll, since a WiFi-less watch would otherwise spin.
    if (unsetSyncTried) {
      return false;
    }
    unsetSyncTried = true;
    return true;
  }
  return secondsUntilSyncDue() == 0;
}

uint32_t RefClock::secondsUntilSyncDue() {
  return RefSyncSchedule::secondsUntilDue(_rtc.epoch(), lastActivity,
                                           lastSyncAttempt, NTP_RESYNC_HOURS,
                                           NTP_MIN_SYNC_INTERVAL_HOURS);
}

float RefClock::batteryVolts() {
  return (analogReadMilliVolts(PIN_BATT_ADC) / 1000.0f) * BATT_DIVIDER;
}

uint8_t RefClock::boardRevision() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  if (info.model != CHIP_ESP32) {
    return 30; // an S3 here means V3
  }
  // V1.0 shipped a DS3231; V1.5 and V2.0 a PCF8563. The two of those are told
  // apart by which pin the battery divider lands on, which is a build-time
  // choice, so fall back to that.
  switch (_rtc.kind()) {
  case RefRtc::DS3231:
    return 10;
#if defined(ARDUINO_WATCHY_V15)
  case RefRtc::PCF8563:
    return 15;
#else
  case RefRtc::PCF8563:
    return 20;
#endif
  default:
    return 0;
  }
}

time_t RefClock::bootEpoch() const { return bootedAt; }
time_t RefClock::lastSyncEpoch() const { return lastSyncOk; }
