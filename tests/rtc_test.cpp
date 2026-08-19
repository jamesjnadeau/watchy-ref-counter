// RefRtc against a stub I2C bus.
//
// Two chips are supported and their register maps disagree in exactly the way
// that produces a plausible wrong answer rather than an obvious failure:
//
//            seconds  minutes  hours  weekday  date  month  year
//   PCF8563   0x02     0x03    0x04    0x06     0x05  0x07   0x08
//   RV-3028   0x00     0x01    0x02    0x03     0x04  0x05   0x06
//
// Note the middle two columns. The PCF8563 puts the date before the weekday
// and the RV-3028 puts the weekday first, so a driver that copies the wrong
// one reads the day of the week as the day of the month -- a number between 0
// and 6, which is a valid-looking date. That is the bug these tests exist for.
//
// Registers and bit positions for the RV-3028 come from the Micro Crystal
// RV-3028-C7 Application Manual; see board-files/UNVERIFIED.md for what has
// and has not been checked against real hardware.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Wire.h>

#include "RefRtc.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

void checkEq(int got, int want, const char *what) {
  if (got != want) {
    printf("FAIL %s: got %d, want %d\n", what, got, want);
    failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

const uint8_t ADDR_PCF8563 = 0x51;
const uint8_t ADDR_RV3028  = 0x52;

// 2026-11-25 21:47:38, a Wednesday: tm_wday 3, tm_mday 25, tm_mon 10.
//
// Every field is >= 10 on purpose. Below that, BCD and binary are the same
// number, so a fixture of 08:05 would pass against a driver that had dropped
// fromBcd() entirely. The month is 11 (BCD 0x11 = 17 read raw, which read()'s
// range check rejects), the hour is 21 (BCD 0x21, which a 12-hour 0x1F mask
// would truncate to 1), and the date and weekday are different numbers that
// neither register could hold by accident.
void loadPcf8563(WireStub::Device &d) {
  d.reg[0x02] = bcd(38);       // seconds, bit 7 (VL) clear
  d.reg[0x03] = bcd(47);       // minutes
  d.reg[0x04] = bcd(21);       // hours
  d.reg[0x05] = bcd(25);       // date
  d.reg[0x06] = 3;             // weekday
  d.reg[0x07] = bcd(11);       // month
  d.reg[0x08] = bcd(26);       // year
}

void loadRv3028(WireStub::Device &d) {
  d.reg[0x00] = bcd(38);       // seconds
  d.reg[0x01] = bcd(47);       // minutes
  d.reg[0x02] = bcd(21);       // hours
  d.reg[0x03] = 3;             // weekday
  d.reg[0x04] = bcd(25);       // date
  d.reg[0x05] = bcd(11);       // month
  d.reg[0x06] = bcd(26);       // year
  d.reg[0x0E] = 0x00;          // status: PORF clear, so the time is good
  d.reg[0x0F] = 0x5A;          // control 1: an arbitrary value begin() must
                               // leave alone
}

void expectNovember25(const struct tm &t, const char *label) {
  char what[96];
  snprintf(what, sizeof(what), "%s: year", label);
  checkEq(t.tm_year, 126, what);
  snprintf(what, sizeof(what), "%s: month (11, whose BCD 0x11 is not 11)", label);
  checkEq(t.tm_mon, 10, what);
  snprintf(what, sizeof(what), "%s: day of month", label);
  checkEq(t.tm_mday, 25, what);
  snprintf(what, sizeof(what), "%s: hour (21, which a 12-hour mask truncates)", label);
  checkEq(t.tm_hour, 21, what);
  snprintf(what, sizeof(what), "%s: minute", label);
  checkEq(t.tm_min, 47, what);
  snprintf(what, sizeof(what), "%s: second", label);
  checkEq(t.tm_sec, 38, what);
}

void testEmptyBus() {
  WireStub::reset();
  RefRtc rtc;
  rtc.begin();
  check(rtc.kind() == RefRtc::NONE, "empty bus: no clock found");

  struct tm t;
  check(!rtc.read(t), "empty bus: read fails");
  check(rtc.epoch() == 0, "empty bus: epoch is 0");
}

void testPcf8563() {
  WireStub::reset();
  loadPcf8563(WireStub::attach(ADDR_PCF8563));

  RefRtc rtc;
  rtc.begin();
  check(rtc.kind() == RefRtc::PCF8563, "PCF8563: probed");

  struct tm t;
  check(rtc.read(t), "PCF8563: read succeeds");
  expectNovember25(t, "PCF8563");
}

void testPcf8563LowVoltage() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_PCF8563);
  loadPcf8563(d);
  d.reg[0x02] |= 0x80; // VL: clock integrity not guaranteed

  RefRtc rtc;
  rtc.begin();
  struct tm t;
  check(!rtc.read(t), "PCF8563: low voltage flag treated as unset");
}

void testRv3028() {
  WireStub::reset();
  loadRv3028(WireStub::attach(ADDR_RV3028));

  RefRtc rtc;
  rtc.begin();
  check(rtc.kind() == RefRtc::RV3028, "RV-3028: probed");

  struct tm t;
  check(rtc.read(t), "RV-3028: read succeeds");
  expectNovember25(t, "RV-3028");
}

// The transposition test. If the driver read the RV-3028 with the PCF8563's
// register order it would take the weekday (3) as the day of the month, so
// point the two registers at values that make the mistake unmistakable.
void testRv3028RegisterOrder() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);
  d.reg[0x03] = 1;       // weekday: Monday
  d.reg[0x04] = bcd(28); // date: the 28th, which no weekday register can hold

  RefRtc rtc;
  rtc.begin();
  struct tm t;
  check(rtc.read(t), "RV-3028: read succeeds");
  checkEq(t.tm_mday, 28, "RV-3028: date comes from 0x04, not the weekday at 0x03");
}

void testRv3028PowerOnResetFlag() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);
  d.reg[0x0E] = 0x01; // PORF: the chip has lost power, the time is meaningless

  RefRtc rtc;
  rtc.begin();
  struct tm t;
  check(!rtc.read(t), "RV-3028: power-on reset flag treated as unset");
}

void testRv3028Begin() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);
  d.reg[0x10] = 0xFF; // every interrupt enabled, as a fresh part might not be

  RefRtc rtc;
  rtc.begin();
  checkEq(d.reg[0x10], 0x00,
          "RV-3028: begin() clears control 2, so INT never asserts");
  checkEq(d.reg[0x0F], 0x5A,
          "RV-3028: begin() leaves control 1 as the part shipped it");
}

void testPcf8563Begin() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_PCF8563);
  loadPcf8563(d);
  d.reg[0x00] = 0xFF;
  d.reg[0x01] = 0xFF;

  RefRtc rtc;
  rtc.begin();
  checkEq(d.reg[0x00], 0x00, "PCF8563: begin() clears control 1");
  checkEq(d.reg[0x01], 0x00, "PCF8563: begin() clears control 2");
}

// set() writes the time and then clears the power-on reset flag in a second
// transaction. If that second one fails, the chip holds the right time but
// goes on calling it unset -- so set() has to report the failure rather than
// claim success. Reporting "not set" is the safe direction: the alternative
// ordering would leave a cleared flag over a stale time, which reads as a
// confidently wrong clock.
void testRv3028SetFlagClearFails() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);
  d.reg[0x0E] = 0x01;

  RefRtc rtc;
  rtc.begin();

  struct tm t = {};
  t.tm_year = 126;
  t.tm_mon  = 11;
  t.tm_mday = 31;
  t.tm_hour = 23;
  t.tm_min  = 59;
  t.tm_sec  = 58;

  // Let the time write through, then refuse the status read-back that follows.
  d.nackSkip = 1;
  d.nacks    = 1;
  check(!rtc.set(t), "RV-3028: set reports failure if the flag clear NACKs");
  checkEq(d.reg[0x00], bcd(58), "RV-3028: the time itself still landed");

  // And a later attempt on a healthy bus recovers, so this is not a dead end.
  check(rtc.set(t), "RV-3028: a retry after the bus recovers succeeds");
  checkEq(d.reg[0x0E] & 0x01, 0, "RV-3028: the retry clears the flag");
}

void testReadTransactionFails() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);

  RefRtc rtc;
  rtc.begin();

  struct tm t;
  d.nacks = 1;
  check(!rtc.read(t), "RV-3028: a NACKed read fails rather than inventing a time");
  check(rtc.read(t), "RV-3028: and the next read recovers");
}

void testRv3028Set() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_RV3028);
  loadRv3028(d);
  d.reg[0x0E] = 0x01; // PORF set, as it would be on a chip that has never run

  RefRtc rtc;
  rtc.begin();

  // 2026-12-31 23:59:58, a Thursday. Every field >= 10 again, so toBcd has to
  // actually run, and December is the month a 0x0F month mask would mangle.
  struct tm t = {};
  t.tm_year = 126; // 2026
  t.tm_mon  = 11;  // December
  t.tm_mday = 31;
  t.tm_hour = 23;
  t.tm_min  = 59;
  t.tm_sec  = 58;
  check(rtc.set(t), "RV-3028: set succeeds");

  checkEq(d.reg[0x00], bcd(58), "RV-3028: seconds written to 0x00 in BCD");
  checkEq(d.reg[0x01], bcd(59), "RV-3028: minutes written to 0x01 in BCD");
  checkEq(d.reg[0x02], bcd(23), "RV-3028: hours written to 0x02 in BCD");
  checkEq(d.reg[0x03], 4, "RV-3028: weekday written to 0x03 as plain binary");
  checkEq(d.reg[0x04], bcd(31), "RV-3028: date written to 0x04 in BCD");
  checkEq(d.reg[0x05], bcd(12), "RV-3028: month written to 0x05 in BCD");
  checkEq(d.reg[0x06], bcd(26), "RV-3028: year written to 0x06 in BCD");
  checkEq(d.reg[0x0E] & 0x01, 0,
          "RV-3028: set clears the power-on reset flag");

  // And the round trip, which is the thing that actually matters.
  struct tm back;
  check(rtc.read(back), "RV-3028: reads back what set wrote");
  checkEq(back.tm_mday, 31, "RV-3028: round trip keeps the date");
  checkEq(back.tm_hour, 23, "RV-3028: round trip keeps the hour");
  checkEq(back.tm_mon, 11, "RV-3028: round trip keeps December");
}

void testPcf8563Set() {
  WireStub::reset();
  WireStub::Device &d = WireStub::attach(ADDR_PCF8563);
  loadPcf8563(d);

  RefRtc rtc;
  rtc.begin();

  struct tm t = {};
  t.tm_year = 126;
  t.tm_mon  = 11; // December
  t.tm_mday = 31;
  t.tm_hour = 23;
  t.tm_min  = 59;
  t.tm_sec  = 58;
  check(rtc.set(t), "PCF8563: set succeeds");

  checkEq(d.reg[0x02], bcd(58), "PCF8563: seconds written to 0x02 in BCD");
  checkEq(d.reg[0x03], bcd(59), "PCF8563: minutes written to 0x03 in BCD");
  checkEq(d.reg[0x04], bcd(23), "PCF8563: hours written to 0x04 in BCD");
  checkEq(d.reg[0x05], bcd(31), "PCF8563: date written to 0x05 in BCD");
  checkEq(d.reg[0x06], 4, "PCF8563: weekday written to 0x06 as plain binary");
  checkEq(d.reg[0x07], bcd(12), "PCF8563: month written to 0x07 in BCD");
}

// Both chips on one bus never happens on real hardware -- no board fits two --
// but the probe has to answer deterministically if it ever did.
void testBothPresent() {
  WireStub::reset();
  loadPcf8563(WireStub::attach(ADDR_PCF8563));
  loadRv3028(WireStub::attach(ADDR_RV3028));

  RefRtc rtc;
  rtc.begin();
  check(rtc.kind() == RefRtc::RV3028,
        "both present: the RV-3028 wins, since only this repo's board has one");
}

} // namespace

int main() {
  // The driver round-trips through mktime()/localtime_r(), so pin the zone.
  setenv("TZ", "UTC0", 1);
  tzset();

  testEmptyBus();
  testPcf8563();
  testPcf8563LowVoltage();
  testRv3028();
  testRv3028RegisterOrder();
  testRv3028PowerOnResetFlag();
  testRv3028Begin();
  testPcf8563Begin();
  testRv3028Set();
  testRv3028SetFlagClearFails();
  testReadTransactionFails();
  testPcf8563Set();
  testBothPresent();

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
