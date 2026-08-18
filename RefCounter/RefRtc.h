#ifndef REF_RTC_H
#define REF_RTC_H

#include <time.h>

#include <Arduino.h>

// The watch's real time clock.
//
// Watchy shipped three different arrangements, so this probes for what is
// actually on the bus:
//   V1.0        DS3231   at 0x68
//   V1.5, V2.0  PCF8563  at 0x51
//   V3          none; the ESP32-S3 keeps time itself on a 32kHz crystal
//
// Both chips are talked to directly over Wire in binary coded decimal. That is
// a handful of register reads, and doing it here keeps the project free of
// the two RTC libraries the reference firmware pulls in -- one of which no
// longer compiles against a current ESP32 core.
//
// Credit for the chip choice and the addresses: sqfmi/Watchy.
class RefRtc {
public:
  enum Kind : uint8_t { NONE, DS3231, PCF8563, INTERNAL };

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

  bool readDS3231(struct tm &out);
  bool readPCF8563(struct tm &out);
  bool present(uint8_t address);
};

#endif // REF_RTC_H
