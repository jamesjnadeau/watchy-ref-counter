// The Current Time Service byte layouts. See RefCtsTime.h for why they live
// apart from the radio, and for the layouts themselves.
#include "RefCtsTime.h"

#include <string.h>

namespace RefCtsTime {
namespace {

const int YEAR_MIN = 2020;
const int YEAR_MAX = 2099;

// A quarter of an hour, the unit both halves of Local Time Information are
// counted in.
const long QUARTER_HOUR = 15 * 60;

// 0x80 as a signed byte. The specification's "time zone offset is not known".
const int8_t TZ_UNKNOWN = -128;

// 0xFF. The specification's "DST offset is not known".
const uint8_t DST_UNKNOWN = 255;

bool leapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int days[12] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && leapYear(year)) {
    return 29;
  }
  return days[month - 1];
}

} // namespace

void clear(Stamp &s) {
  memset(&s, 0, sizeof(s));
}

int dayOfWeek(int year, int month, int day) {
  // Sakamoto's method. Valid for any date from 1752 on, which is well outside
  // the range parseCurrentTime accepts.
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) {
    y -= 1;
  }
  return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

bool parseCurrentTime(const uint8_t *data, size_t len, struct tm &out) {
  if (data == nullptr || len < CURRENT_TIME_MIN_LEN) {
    return false;
  }

  const int year   = (int)data[0] | ((int)data[1] << 8); // little endian
  const int month  = data[2];
  const int day    = data[3];
  const int hour   = data[4];
  const int minute = data[5];
  const int second = data[6];

  if (year < YEAR_MIN || year > YEAR_MAX) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  if (day < 1 || day > daysInMonth(year, month)) {
    return false;
  }
  // Leap seconds are not represented: the specification's seconds field stops
  // at 59, and neither RTC chip can hold a 60 anyway.
  if (hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  // The day of week and the fraction of a second are read past, not used.
  // The day of week is recomputed here rather than trusted -- it is redundant
  // with the date, and a client that fills it in wrongly should not be able to
  // put a wrong weekday into the RTC. The fraction is dropped rather than
  // rounded: it is worth under half a second, and rounding it up would mean
  // carrying across a minute, an hour, a day and a year boundary for nothing.
  memset(&out, 0, sizeof(out));
  out.tm_year  = year - 1900;
  out.tm_mon   = month - 1;
  out.tm_mday  = day;
  out.tm_hour  = hour;
  out.tm_min   = minute;
  out.tm_sec   = second;
  out.tm_wday  = dayOfWeek(year, month, day);
  out.tm_isdst = 0;
  return true;
}

bool parseLocalTimeInfo(const uint8_t *data, size_t len, long &offsetSeconds) {
  if (data == nullptr || len < LOCAL_TIME_INFO_LEN) {
    return false;
  }

  const int8_t  zone = (int8_t)data[0];
  const uint8_t dst  = data[1];

  if (zone == TZ_UNKNOWN) {
    return false; // no zone means no way to turn the wall clock into UTC
  }
  // A phone that knows its zone but not its daylight saving offset is still
  // useful: the zone field alone is worth an hour of accuracy, and treating
  // the unknown as zero is what the rest of the world does with it.
  const long dstSteps = (dst == DST_UNKNOWN) ? 0 : (long)dst;

  offsetSeconds = ((long)zone + dstSteps) * QUARTER_HOUR;
  return true;
}

size_t encodeCurrentTime(const struct tm &t, uint8_t adjustReason,
                         uint8_t *out, size_t n) {
  if (out == nullptr || n < CURRENT_TIME_LEN) {
    return 0;
  }
  const int year  = t.tm_year + 1900;
  const int month = t.tm_mon + 1;

  out[0] = (uint8_t)(year & 0xFF);
  out[1] = (uint8_t)((year >> 8) & 0xFF);
  out[2] = (uint8_t)month;
  out[3] = (uint8_t)t.tm_mday;
  out[4] = (uint8_t)t.tm_hour;
  out[5] = (uint8_t)t.tm_min;
  out[6] = (uint8_t)t.tm_sec;
  // struct tm counts Sunday as 0; the characteristic counts Monday as 1 and
  // Sunday as 7.
  const int wday = dayOfWeek(year, month, t.tm_mday);
  out[7] = (uint8_t)(wday == 0 ? 7 : wday);
  out[8] = 0; // fractions of a second; the RTC has none to report
  out[9] = adjustReason;
  return CURRENT_TIME_LEN;
}

size_t encodeLocalTimeInfo(long offsetSeconds, uint8_t *out, size_t n) {
  if (out == nullptr || n < LOCAL_TIME_INFO_LEN) {
    return 0;
  }
  // Truncation towards zero, so a negative offset rounds the same way a
  // positive one does. Every US zone is a whole number of quarter hours, so
  // nothing is actually lost here.
  long steps = offsetSeconds / QUARTER_HOUR;
  if (steps > 127 - 1) {
    steps = 127 - 1;
  } else if (steps < -127) {
    steps = -127;
  }
  // The watch folds daylight saving into the one offset it reports, so the
  // whole of it goes in the zone field and the DST field is left at zero.
  // Splitting them would mean claiming a standard-time offset the watch does
  // not separately track.
  out[0] = (uint8_t)(int8_t)steps;
  out[1] = 0;
  return LOCAL_TIME_INFO_LEN;
}

} // namespace RefCtsTime
