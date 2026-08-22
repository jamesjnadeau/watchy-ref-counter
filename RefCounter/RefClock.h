#ifndef REF_CLOCK_H
#define REF_CLOCK_H

#include <time.h>

#include "RefBleTime.h"
#include "RefCtsTime.h"
#include "RefRtc.h"

// Timekeeping: the RTC chip, the two ways the clock gets set from outside,
// and the odds and ends the About screen reports.
//
// The approach is the one the reference firmware uses -- keep the time in the
// RTC, correct its drift from an outside source -- but none of its code. NTP
// goes through the ESP32 core's own SNTP client rather than a library. The
// second source, a phone over Bluetooth, has no counterpart there at all; the
// radio half of it is in RefBleTime and the byte layouts in RefCtsTime, and
// what lives here is the conversion into the RTC's UTC and the bookkeeping
// the resync schedule and the About screen read.
class RefClock {
public:
  // Where the clock was last set from, for the About screen.
  enum SyncSource : uint8_t { SYNC_NONE, SYNC_NTP, SYNC_BT };

  // Brings up I2C and probes for the RTC. Pass true on a cold boot to stamp
  // the boot time used by the About screen.
  void begin(bool coldBoot);

  RefRtc &rtc() { return _rtc; }

  // Current hour and minute, local. False if the clock has never been set, in
  // which case the caller should show --:-- rather than a made up time.
  bool read(uint8_t &hour, uint8_t &minute);

  // The full local date and time. The RTC itself holds UTC; the zone offset
  // and any daylight saving are applied here, so nothing downstream has to
  // know about either. False if the clock has never been set.
  bool localNow(struct tm &out);

  // Set the clock from a local date and time, as typed on the set-time
  // screen. Converts to UTC before writing.
  bool setLocal(const struct tm &local);

  // Seconds currently being added to UTC, and the matching abbreviation
  // ("EST", "EDT"), for the screens that report the zone.
  long utcOffsetSeconds();
  const char *zoneAbbrev();

  // Set the RTC from an internet time server. Assumes WiFi is already up; the
  // menu connects itself so it can report each step. Records the attempt
  // either way.
  bool syncFromNtp();

  // An automatic sync: NTP over WiFi, and then -- only if that produced
  // nothing and BT_AUTO_SYNC_SECONDS is not 0 -- a Bluetooth window. Blocks
  // for several seconds, and for as long again as that window is open. The
  // two radios are never up at once.
  bool connectAndSync();

  // Set the clock from a Current Time value a phone has written.
  //
  // The phone writes its own local wall clock, so this has to turn that into
  // the UTC the RTC holds. When the phone sent its zone as well that offset
  // is used, and the watch's own TZ setting does not enter into it; when it
  // did not, the wall clock is taken as local to whatever zone the watch is
  // set to, exactly as a time typed into the Set Time screen would be.
  bool applyBleStamp(const RefCtsTime::Stamp &stamp);

  // Advertise for up to windowMs and set the clock if a phone writes to it.
  // Used by the automatic path; the menu runs its own loop instead, so it can
  // draw progress and let BACK cancel, and calls applyBleStamp directly.
  bool syncFromBluetooth(uint32_t windowMs);

  // Publish the watch's own time on the Bluetooth characteristics, so a
  // phone that reads them sees a real clock rather than an empty value.
  // Takes local wall time; does nothing while no sync window is open.
  void publishBleTime(const struct tm &local);

  // Record a sync attempt that set nothing. The schedule counts its minimum
  // interval from the last attempt, not the last success, so a window that
  // closed empty still has to be stamped.
  void noteSyncAttempt();

  // Stamps the moment of a button press or sleep entry. This is the anchor an
  // automatic resync counts its quiet period from, so the sketch calls it on
  // every button-driven state change and on sleep entry.
  void noteActivity();

  // True once the watch has sat untouched for NTP_RESYNC_HOURS and at least
  // NTP_MIN_SYNC_INTERVAL_HOURS have passed since the last attempt. Also true,
  // once per wake cycle, if the RTC has never been set at all.
  bool syncDue();

  // Seconds until syncDue() would next return true, for arming the deep-sleep
  // timer. RefSyncSchedule::NEVER if no automatic sync is due at all.
  uint32_t secondsUntilSyncDue();

  // Battery terminal voltage, and 10 / 15 / 20 / 30 for the board revision.
  float batteryVolts();
  uint8_t boardRevision();

  time_t bootEpoch() const;
  time_t lastSyncEpoch() const;
  SyncSource lastSyncSource() const;

private:
  RefRtc _rtc;
};

#endif // REF_CLOCK_H
