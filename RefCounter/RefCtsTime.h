#ifndef REF_CTS_TIME_H
#define REF_CTS_TIME_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// The Bluetooth Current Time Service payloads, decoded.
//
// A phone hands the watch the time by writing two standard GATT
// characteristics, both defined by the Bluetooth SIG:
//
//   0x2A2B  Current Time            10 bytes  the wall clock itself
//   0x2A0F  Local Time Information   2 bytes  the zone that clock is in
//
// RefBleTime.cpp owns the radio and the GATT server; this file owns the bytes.
// It is deliberately free of every Arduino and ESP-IDF header, for the same
// reason RefSyncSchedule and RefZone are: a byte layout with a dozen range
// checks in it is exactly the sort of thing that is cheap to test on a host
// and expensive to test by holding a phone next to a watch.
//
// Layouts, from the SIG's characteristic definitions:
//
//   Current Time = Exact Time 256 + Adjust Reason
//     Exact Time 256 = Day Date Time + Fractions256
//       Day Date Time = Date Time + Day of Week
//         Date Time = year uint16 LE, month, day, hours, minutes, seconds
//         Day of Week = 1 (Monday) .. 7 (Sunday), 0 unknown
//       Fractions256 = 1/256ths of a second
//     Adjust Reason = a bitfield saying why the phone is writing
//
//   Local Time Information = time zone (signed, 15 minute steps)
//                          + DST offset (unsigned, 15 minute steps)
namespace RefCtsTime {

// A full Current Time write. Anything shorter than the Date Time part is
// rejected; the trailing fields are optional, so a client that writes only
// the 7 byte date and time is still understood.
static const size_t CURRENT_TIME_LEN     = 10;
static const size_t CURRENT_TIME_MIN_LEN = 7;
static const size_t LOCAL_TIME_INFO_LEN  = 2;

// Adjust Reason bits, for the value the watch publishes when read.
static const uint8_t ADJUST_MANUAL          = 0x01;
static const uint8_t ADJUST_EXTERNAL_REF    = 0x02;
static const uint8_t ADJUST_TIME_ZONE       = 0x04;
static const uint8_t ADJUST_DST             = 0x08;

// What a phone told the watch.
//
// `wall` is whatever clock the phone wrote, which by the service's convention
// is its own local time, not UTC. Whether the watch can turn that into UTC
// exactly depends on the second characteristic: with Local Time Information
// in hand the offset is the phone's own, and the watch's zone setting does not
// enter into it; without it the caller has to assume the phone is in the zone
// the watch is set to.
struct Stamp {
  struct tm wall;
  bool      haveOffset;
  long      offsetSeconds;
};

// Zero `s` into the "nothing received" state.
void clear(Stamp &s);

// Decode a Current Time write into `out`. False -- leaving `out` untouched --
// for a short write, or for any field outside its range.
//
// The year is held to 2020..2099 rather than the specification's 1582..9999.
// Both RTC chips store the year as two BCD digits, so a year outside that
// century cannot be represented anyway, and the low end rejects the one write
// that is otherwise plausible and always wrong: a phone whose own clock has
// not been set yet.
bool parseCurrentTime(const uint8_t *data, size_t len, struct tm &out);

// Decode a Local Time Information write. `offsetSeconds` comes back as the
// total seconds to add to UTC -- zone and daylight saving folded together,
// which is the form RefZone and RefClock already work in. False for a short
// write or an unknown zone.
bool parseLocalTimeInfo(const uint8_t *data, size_t len, long &offsetSeconds);

// Encode `t` as a 10 byte Current Time value, so a phone that reads the
// characteristic sees the watch's current time rather than an empty value.
// Returns the number of bytes written, or 0 if `n` is too small.
size_t encodeCurrentTime(const struct tm &t, uint8_t adjustReason,
                         uint8_t *out, size_t n);

// The same for Local Time Information, from a UTC offset in seconds. Offsets
// that are not a whole number of 15 minute steps are rounded towards zero,
// which loses nothing: no zone in RefZone.cpp is finer than that.
size_t encodeLocalTimeInfo(long offsetSeconds, uint8_t *out, size_t n);

// Day of week for a date, 0 = Sunday, matching struct tm's tm_wday. Exposed
// because both of the encoders need it and it is worth a test of its own.
int dayOfWeek(int year, int month, int day);

} // namespace RefCtsTime

#endif // REF_CTS_TIME_H
