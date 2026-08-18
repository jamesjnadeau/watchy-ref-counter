#include "RefClock.h"

#include <WiFi.h>
#include <Wire.h>
#include <esp_chip_info.h>

#include "board.h"
#include "settings.h"

namespace {

// All three survive deep sleep, so low power mode neither restarts the resync
// interval nor resets the uptime shown in the menu.
RTC_DATA_ATTR time_t lastSyncAttempt = 0;
RTC_DATA_ATTR time_t lastSyncOk      = 0;
RTC_DATA_ATTR time_t bootedAt        = 0;

} // namespace

void RefClock::begin(bool coldBoot) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  _rtc.begin();

  // The SNTP client applies this, and so does localtime_r once the RTC has
  // pushed a value through settimeofday().
  setenv("TZ", "UTC0", 1);
  tzset();

  if (coldBoot) {
    bootedAt = _rtc.epoch();
  }
}

bool RefClock::read(uint8_t &hour, uint8_t &minute) {
  struct tm t;
  if (!_rtc.read(t)) {
    return false;
  }
  hour   = (uint8_t)t.tm_hour;
  minute = (uint8_t)t.tm_min;
  return true;
}

bool RefClock::syncFromNtp() {
  // The core's SNTP client. The offset is applied here rather than by a
  // timezone rule, so the RTC ends up holding local time.
  configTime(GMT_OFFSET_SECONDS, 0, NTP_SERVER);

  struct tm fetched;
  const bool ok = getLocalTime(&fetched, NTP_TIMEOUT_MS);
  if (ok) {
    _rtc.set(fetched);
  }

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

bool RefClock::syncDue() {
  if (NTP_RESYNC_HOURS == 0) {
    return false;
  }
  if (lastSyncAttempt == 0) {
    return true; // never tried since the battery went in
  }
  const time_t now = _rtc.epoch();
  if (now == 0 || now < lastSyncAttempt) {
    return true; // unset, or the clock moved backwards
  }
  return (uint32_t)(now - lastSyncAttempt) >= NTP_RESYNC_HOURS * 3600UL;
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
