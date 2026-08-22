// Host test for the Bluetooth Current Time Service payloads in RefCtsTime.cpp.
//
// This is the file where a wrong byte costs the most and shows the least: a
// phone writes ten bytes, the watch either believes them or does not, and the
// only symptom on the wrist is a clock an hour or a century out. Everything
// here is arithmetic and range checking, so it all belongs on a host.
//
// Layouts under test, from the Bluetooth SIG:
//   Current Time (0x2A2B): year LE, month, day, hour, minute, second,
//                          day of week, fractions256, adjust reason
//   Local Time Information (0x2A0F): time zone, DST offset, both in 15
//                          minute steps
#include "RefCtsTime.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void expectEq(const char *what, long got, long want) {
  if (got != want) {
    printf("FAIL %-46s want %ld got %ld\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %ld\n", what, got);
  }
}

static void expectBool(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-46s want %s got %s\n", what, want ? "true" : "false",
           got ? "true" : "false");
    failures++;
  } else {
    printf("ok   %-46s %s\n", what, got ? "true" : "false");
  }
}

// A well formed Current Time write: 2026-08-22 14:35:07, a Saturday, with
// half a second of fraction and the "manual update" adjust reason.
static void goodCurrentTime(uint8_t *b) {
  b[0] = 2026 & 0xFF;
  b[1] = (2026 >> 8) & 0xFF;
  b[2] = 8;
  b[3] = 22;
  b[4] = 14;
  b[5] = 35;
  b[6] = 7;
  b[7] = 6; // Saturday, in the characteristic's Monday-is-1 numbering
  b[8] = 128;
  b[9] = RefCtsTime::ADJUST_MANUAL;
}

static void expectRejected(const char *what, uint8_t index, uint8_t value) {
  uint8_t b[RefCtsTime::CURRENT_TIME_LEN];
  goodCurrentTime(b);
  b[index] = value;
  struct tm out;
  expectBool(what, RefCtsTime::parseCurrentTime(b, sizeof(b), out), false);
}

int main() {
  uint8_t b[RefCtsTime::CURRENT_TIME_LEN];
  struct tm out;

  // --- the happy path ------------------------------------------------------
  goodCurrentTime(b);
  expectBool("full write parses", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             true);
  expectEq("year", out.tm_year + 1900, 2026);
  expectEq("month", out.tm_mon + 1, 8);
  expectEq("day", out.tm_mday, 22);
  expectEq("hour", out.tm_hour, 14);
  expectEq("minute", out.tm_min, 35);
  expectEq("second", out.tm_sec, 7);
  // Recomputed from the date, not read out of the packet: struct tm counts
  // Sunday as 0, and 2026-08-22 is a Saturday.
  expectEq("weekday is computed, Sunday is 0", out.tm_wday, 6);
  expectEq("dst flag is off", out.tm_isdst, 0);

  // A client that fills the day of week in wrongly must not be able to put a
  // wrong weekday into the RTC.
  goodCurrentTime(b);
  b[7] = 3; // claims Wednesday
  RefCtsTime::parseCurrentTime(b, sizeof(b), out);
  expectEq("a wrong weekday in the packet is ignored", out.tm_wday, 6);

  // --- short writes --------------------------------------------------------
  // The trailing fields are optional: a client that writes only the date and
  // time is understood, one that stops inside it is not.
  goodCurrentTime(b);
  expectBool("seven bytes is enough",
             RefCtsTime::parseCurrentTime(b, 7, out), true);
  expectEq("seven byte write still carries the second", out.tm_sec, 7);
  expectBool("six bytes is not", RefCtsTime::parseCurrentTime(b, 6, out),
             false);
  expectBool("no bytes at all", RefCtsTime::parseCurrentTime(b, 0, out), false);
  expectBool("null data", RefCtsTime::parseCurrentTime(nullptr, 10, out),
             false);

  // A rejected write must leave the caller's struct alone, so a bad packet
  // can never half-set the clock.
  goodCurrentTime(b);
  RefCtsTime::parseCurrentTime(b, sizeof(b), out);
  struct tm before = out;
  uint8_t bad[RefCtsTime::CURRENT_TIME_LEN];
  goodCurrentTime(bad);
  bad[2] = 13; // month 13
  RefCtsTime::parseCurrentTime(bad, sizeof(bad), out);
  expectEq("a rejected write leaves the output alone",
           memcmp(&before, &out, sizeof(out)) == 0, 1);

  // --- field ranges --------------------------------------------------------
  expectRejected("month 0 rejected", 2, 0);
  expectRejected("month 13 rejected", 2, 13);
  expectRejected("day 0 rejected", 3, 0);
  expectRejected("day 32 rejected", 3, 32);
  expectRejected("hour 24 rejected", 4, 24);
  expectRejected("minute 60 rejected", 5, 60);
  expectRejected("second 60 rejected", 6, 60); // no leap seconds to store

  // The year is held to the century the RTC chips can represent, which also
  // throws out the one plausible wrong write: a phone whose own clock has
  // never been set.
  goodCurrentTime(b);
  b[0] = 1970 & 0xFF;
  b[1] = (1970 >> 8) & 0xFF;
  expectBool("1970 rejected", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             false);
  b[0] = 2019 & 0xFF;
  b[1] = (2019 >> 8) & 0xFF;
  expectBool("2019 rejected", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             false);
  b[0] = 2020 & 0xFF;
  b[1] = (2020 >> 8) & 0xFF;
  expectBool("2020 accepted", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             true);
  b[0] = 2099 & 0xFF;
  b[1] = (2099 >> 8) & 0xFF;
  expectBool("2099 accepted", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             true);
  b[0] = 2100 & 0xFF;
  b[1] = (2100 >> 8) & 0xFF;
  expectBool("2100 rejected", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             false);

  // --- month lengths, including the leap year rule -------------------------
  goodCurrentTime(b);
  b[2] = 2;
  b[3] = 29;
  b[0] = 2024 & 0xFF;
  b[1] = (2024 >> 8) & 0xFF;
  expectBool("29 February 2024 accepted",
             RefCtsTime::parseCurrentTime(b, sizeof(b), out), true);
  b[0] = 2026 & 0xFF;
  b[1] = (2026 >> 8) & 0xFF;
  expectBool("29 February 2026 rejected",
             RefCtsTime::parseCurrentTime(b, sizeof(b), out), false);
  b[0] = 2100 & 0xFF; // a century that is not a leap year -- and out of range
  b[1] = (2100 >> 8) & 0xFF;
  expectBool("29 February 2100 rejected",
             RefCtsTime::parseCurrentTime(b, sizeof(b), out), false);
  goodCurrentTime(b);
  b[2] = 4;
  b[3] = 31; // April has 30
  expectBool("31 April rejected", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             false);
  b[3] = 30;
  expectBool("30 April accepted", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             true);

  // Midnight is a legal time, and the one an off-by-one in the hour check
  // would throw away.
  goodCurrentTime(b);
  b[4] = 0;
  b[5] = 0;
  b[6] = 0;
  expectBool("midnight accepted", RefCtsTime::parseCurrentTime(b, sizeof(b), out),
             true);
  expectEq("midnight hour", out.tm_hour, 0);

  // --- Local Time Information ----------------------------------------------
  long offset = 12345;
  uint8_t lti[RefCtsTime::LOCAL_TIME_INFO_LEN];

  lti[0] = (uint8_t)(int8_t)(-20); // -5 hours, US Eastern standard time
  lti[1] = 0;
  expectBool("eastern standard parses",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), true);
  expectEq("eastern standard offset", offset, -5 * 3600);

  lti[1] = 4; // one hour of daylight saving, folded into the one offset
  expectBool("eastern daylight parses",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), true);
  expectEq("eastern daylight offset", offset, -4 * 3600);

  lti[0] = 22; // +5:30, the half hour zone that catches sloppy arithmetic
  lti[1] = 0;
  expectBool("half hour zone parses",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), true);
  expectEq("half hour zone offset", offset, 5 * 3600 + 30 * 60);

  // A phone that knows its zone but not its daylight saving offset is still
  // worth listening to; one that knows neither is not.
  lti[0] = (uint8_t)(int8_t)(-20);
  lti[1] = 255;
  expectBool("unknown dst still parses",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), true);
  expectEq("unknown dst counts as none", offset, -5 * 3600);

  lti[0] = 0x80; // the specification's "zone not known"
  lti[1] = 0;
  expectBool("unknown zone rejected",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), false);
  expectBool("short zone write rejected",
             RefCtsTime::parseLocalTimeInfo(lti, 1, offset), false);
  expectBool("null zone write rejected",
             RefCtsTime::parseLocalTimeInfo(nullptr, 2, offset), false);

  // UTC itself: zero is a real offset, not a missing one.
  lti[0] = 0;
  lti[1] = 0;
  expectBool("utc parses", RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset),
             true);
  expectEq("utc offset", offset, 0);

  // --- what the watch publishes back ---------------------------------------
  struct tm now;
  memset(&now, 0, sizeof(now));
  now.tm_year = 2026 - 1900;
  now.tm_mon  = 0;
  now.tm_mday = 4; // a Sunday, the day the two weekday numberings disagree on
  now.tm_hour = 9;
  now.tm_min  = 5;
  now.tm_sec  = 3;

  uint8_t enc[RefCtsTime::CURRENT_TIME_LEN];
  expectEq("encode writes ten bytes",
           (long)RefCtsTime::encodeCurrentTime(now, RefCtsTime::ADJUST_MANUAL,
                                               enc, sizeof(enc)),
           (long)RefCtsTime::CURRENT_TIME_LEN);
  expectEq("encoded year low byte", enc[0], 2026 & 0xFF);
  expectEq("encoded year high byte", enc[1], (2026 >> 8) & 0xFF);
  expectEq("encoded month", enc[2], 1);
  expectEq("encoded day", enc[3], 4);
  expectEq("encoded hour", enc[4], 9);
  expectEq("encoded minute", enc[5], 5);
  expectEq("encoded second", enc[6], 3);
  expectEq("Sunday encodes as seven, not zero", enc[7], 7);
  expectEq("encoded fractions", enc[8], 0);
  expectEq("encoded adjust reason", enc[9], RefCtsTime::ADJUST_MANUAL);
  expectEq("encode refuses a short buffer",
           (long)RefCtsTime::encodeCurrentTime(now, 0, enc, 9), 0);

  // Round trip: what the watch publishes, it must be able to read back.
  expectBool("published time parses",
             RefCtsTime::parseCurrentTime(enc, sizeof(enc), out), true);
  expectEq("round trip year", out.tm_year, now.tm_year);
  expectEq("round trip month", out.tm_mon, now.tm_mon);
  expectEq("round trip day", out.tm_mday, now.tm_mday);
  expectEq("round trip hour", out.tm_hour, now.tm_hour);
  expectEq("round trip minute", out.tm_min, now.tm_min);
  expectEq("round trip second", out.tm_sec, now.tm_sec);

  expectEq("zone encode writes two bytes",
           (long)RefCtsTime::encodeLocalTimeInfo(-4 * 3600, lti, sizeof(lti)),
           (long)RefCtsTime::LOCAL_TIME_INFO_LEN);
  expectEq("zone encodes in quarter hours", (int8_t)lti[0], -16);
  expectEq("the whole offset goes in the zone field", lti[1], 0);
  expectEq("zone encode refuses a short buffer",
           (long)RefCtsTime::encodeLocalTimeInfo(0, lti, 1), 0);

  // Published zone and parsed zone have to agree, or a phone reading the
  // watch and a phone writing to it would disagree about the same offset.
  RefCtsTime::encodeLocalTimeInfo(-9 * 3600 - 30 * 60, lti, sizeof(lti));
  expectBool("published zone parses",
             RefCtsTime::parseLocalTimeInfo(lti, sizeof(lti), offset), true);
  expectEq("published zone round trips", offset, -9 * 3600 - 30 * 60);

  // --- the weekday helper --------------------------------------------------
  expectEq("2026-08-22 is a Saturday", RefCtsTime::dayOfWeek(2026, 8, 22), 6);
  expectEq("2026-01-04 is a Sunday", RefCtsTime::dayOfWeek(2026, 1, 4), 0);
  expectEq("2024-02-29 is a Thursday", RefCtsTime::dayOfWeek(2024, 2, 29), 4);
  expectEq("2024-03-01 is a Friday", RefCtsTime::dayOfWeek(2024, 3, 1), 5);
  expectEq("2000-01-01 is a Saturday", RefCtsTime::dayOfWeek(2000, 1, 1), 6);

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
