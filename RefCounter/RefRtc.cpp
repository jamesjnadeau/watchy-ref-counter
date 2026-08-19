#include "RefRtc.h"

#include <Wire.h>
#include <sys/time.h>

#include "board.h"

namespace {

const uint8_t ADDR_PCF8563 = 0x51;

uint8_t fromBcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
uint8_t toBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool readRegisters(uint8_t address, uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(address, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

bool writeRegisters(uint8_t address, uint8_t reg, const uint8_t *buf,
                    uint8_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(buf, len);
  return Wire.endTransmission() == 0;
}

// Fill in tm_wday and normalise, so callers only have to supply the date.
void normalise(struct tm &t) {
  t.tm_isdst   = -1;
  const time_t e = mktime(&t);
  if (e != (time_t)-1) {
    struct tm copy;
    localtime_r(&e, &copy);
    t = copy;
  }
}

} // namespace

bool RefRtc::present(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void RefRtc::begin() {
  if (present(ADDR_PCF8563)) {
    _kind = PCF8563;
    // Control 1 and 2 cleared: run normally, no alarm or timer interrupts.
    const uint8_t control[2] = {0x00, 0x00};
    writeRegisters(ADDR_PCF8563, 0x00, control, 2);
  } else {
    _kind = NONE;
  }
}

// PCF8563 registers 0x02..0x08: seconds (bit 7 is the low voltage flag),
// minutes, hours, day, weekday, month (bit 7 is the century flag), year.
bool RefRtc::readPCF8563(struct tm &out) {
  uint8_t r[7];
  if (!readRegisters(ADDR_PCF8563, 0x02, r, 7)) {
    return false;
  }
  if (r[0] & 0x80) {
    return false; // clock integrity not guaranteed; treat as unset
  }
  out.tm_sec  = fromBcd(r[0] & 0x7F);
  out.tm_min  = fromBcd(r[1] & 0x7F);
  out.tm_hour = fromBcd(r[2] & 0x3F);
  out.tm_mday = fromBcd(r[3] & 0x3F);
  out.tm_mon  = fromBcd(r[5] & 0x1F) - 1;
  out.tm_year = fromBcd(r[6]) + 100;
  return true;
}

bool RefRtc::read(struct tm &out) {
  bool ok = false;
  switch (_kind) {
  case PCF8563:
    ok = readPCF8563(out);
    break;
  default:
    return false;
  }
  if (!ok) {
    return false;
  }
  // A chip that has never been set reports a month of 0, which decrements to
  // -1 above. Catch that and anything else out of range.
  if (out.tm_mon < 0 || out.tm_mon > 11 || out.tm_mday < 1 ||
      out.tm_mday > 31 || out.tm_hour > 23 || out.tm_min > 59) {
    return false;
  }
  normalise(out);
  return true;
}

bool RefRtc::set(const struct tm &in) {
  struct tm t = in;
  normalise(t);

  // Keep the SoC's own clock in step whatever else is fitted, so time() and
  // anything built on it agree with the chip.
  struct timeval tv = {};
  struct tm copy    = t;
  tv.tv_sec         = mktime(&copy);
  settimeofday(&tv, nullptr);

  switch (_kind) {
  case PCF8563: {
    const uint8_t r[7] = {
        toBcd((uint8_t)t.tm_sec), // writing clears the low voltage flag
        toBcd((uint8_t)t.tm_min),        toBcd((uint8_t)t.tm_hour),
        toBcd((uint8_t)t.tm_mday),       (uint8_t)t.tm_wday,
        toBcd((uint8_t)(t.tm_mon + 1)),  toBcd((uint8_t)(t.tm_year % 100)),
    };
    return writeRegisters(ADDR_PCF8563, 0x02, r, 7);
  }
  default:
    return false;
  }
}

time_t RefRtc::epoch() {
  struct tm t;
  if (!read(t)) {
    return 0;
  }
  return mktime(&t);
}
