#ifndef REF_RTC_H
#define REF_RTC_H

#include <time.h>

#include <Arduino.h>

// The watch's real time clock.
//
// Which one that is depends on the board, so this probes for what is actually
// on the bus:
//   V2.0  PCF8563    at 0x51
//   C6    RV-3028-C7 at 0x52
//
// Both are talked to directly over Wire in binary coded decimal. That is a
// handful of register reads each, and doing it here keeps the project free of
// the RTC libraries the reference firmware pulls in -- one of which no longer
// compiles against a current ESP32 core.
//
// Their register maps are not the same shape, and the difference is a trap:
// the PCF8563 puts the date at 0x05 and the weekday at 0x06, while the
// RV-3028 puts the weekday at 0x03 and the date at 0x04. Reading one with the
// other's layout yields a day of the month between 0 and 6 -- wrong, and
// wrong in a way that looks like a date. tests/rtc_test.cpp pins both.
//
// Credit for the PCF8563 choice and address: sqfmi/Watchy. The RV-3028-C7 is
// this repo's own board; see docs/superpowers/specs/ for why it was picked.
class RefRtc {
public:
  enum Kind : uint8_t { NONE, PCF8563, RV3028 };

  // Probes the bus. Wire must already be started.
  void begin();

  Kind kind() const { return _kind; }

  // Local wall clock. False if no clock could be read, or if it holds an
  // obviously invalid time (an unset chip reports month 0).
  bool read(struct tm &out);

  // Set the clock. `t` is local time; tm_wday is computed here.
  bool set(const struct tm &t);

  // Seconds since the epoch, or 0 if the clock is not set.
  time_t epoch();

private:
  Kind _kind = NONE;

  bool readPCF8563(struct tm &out);
  bool readRV3028(struct tm &out);
  bool present(uint8_t address);
};

#endif // REF_RTC_H
