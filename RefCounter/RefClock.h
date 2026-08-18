#ifndef REF_CLOCK_H
#define REF_CLOCK_H

#include <time.h>

#include "RefRtc.h"

// Timekeeping: the RTC chip, NTP sync over WiFi, and the odds and ends the
// About screen reports.
//
// The approach is the one the reference firmware uses -- keep the time in the
// RTC, correct its drift from an internet time server -- but none of its code.
// NTP goes through the ESP32 core's own SNTP client rather than a library.
class RefClock {
public:
  // Brings up I2C and probes for the RTC. Pass true on a cold boot to stamp
  // the boot time used by the About screen.
  void begin(bool coldBoot);

  RefRtc &rtc() { return _rtc; }

  // Current hour and minute. False if the clock has never been set, in which
  // case the caller should show --:-- rather than a made up time.
  bool read(uint8_t &hour, uint8_t &minute);

  // Set the RTC from an internet time server. Assumes WiFi is already up; the
  // menu connects itself so it can report each step. Records the attempt
  // either way.
  bool syncFromNtp();

  // Connect, sync, drop the radio. Blocks for several seconds.
  bool connectAndSync();

  // True once NTP_RESYNC_HOURS have passed since the last attempt.
  bool syncDue();

  // Battery terminal voltage, and 10 / 15 / 20 / 30 for the board revision.
  float batteryVolts();
  uint8_t boardRevision();

  time_t bootEpoch() const;
  time_t lastSyncEpoch() const;

private:
  RefRtc _rtc;
};

#endif // REF_CLOCK_H
