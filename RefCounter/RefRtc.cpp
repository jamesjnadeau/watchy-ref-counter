#include "RefRtc.h"

#include <Wire.h>
#include <sys/time.h>

namespace {

const uint8_t ADDR_PCF8563 = 0x51;
const uint8_t ADDR_RV3028  = 0x52;

// RV-3028 registers. Time is 0x00..0x06 -- seconds, minutes, hours, weekday,
// date, month, year -- and 0x0E..0x10 are status, control 1 and control 2.
// Application Manual, Rev. 1.4, register map.
const uint8_t RV3028_TIME    = 0x00;
const uint8_t RV3028_STATUS  = 0x0E;
const uint8_t RV3028_CONTROL2 = 0x10;

// Status bit 0. Set when the chip powers up, and stays set until software
// clears it, so it is how an RV-3028 says its time means nothing -- the same
// job the PCF8563's low voltage flag does.
const uint8_t RV3028_PORF = 0x01;

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
  // The RV-3028 first: it is what this repo's own board fits, and no board
  // carries both, so the order only decides a case that cannot happen.
  if (present(ADDR_RV3028)) {
    _kind = RV3028;
    // Control 2 cleared: 24 hour mode, and every interrupt enable off so the
    // INT line stays deasserted. This firmware never programs an alarm, and
    // an alarm nobody clears would hold INT low for good -- which matters
    // because that pin is wired to a wake-capable GPIO on the C6 board.
    // Control 1 is left alone: it holds the EEPROM refresh settings, and
    // nothing here touches the EEPROM.
    const uint8_t control2 = 0x00;
    writeRegisters(ADDR_RV3028, RV3028_CONTROL2, &control2, 1);
  } else if (present(ADDR_PCF8563)) {
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

// RV-3028 registers 0x00..0x06: seconds, minutes, hours, weekday, date, month
// (no century bit -- the chip has a full century counter it is not worth
// reading), year. Note the weekday and date order, which is the reverse of the
// PCF8563's.
bool RefRtc::readRV3028(struct tm &out) {
  uint8_t status;
  if (!readRegisters(ADDR_RV3028, RV3028_STATUS, &status, 1)) {
    return false;
  }
  if (status & RV3028_PORF) {
    return false; // powered up since the time was last set; treat as unset
  }

  uint8_t r[7];
  if (!readRegisters(ADDR_RV3028, RV3028_TIME, r, 7)) {
    return false;
  }
  out.tm_sec  = fromBcd(r[0] & 0x7F);
  out.tm_min  = fromBcd(r[1] & 0x7F);
  out.tm_hour = fromBcd(r[2] & 0x3F); // 24 hour mode, forced in begin()
  out.tm_mday = fromBcd(r[4] & 0x3F);
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
  case RV3028:
    ok = readRV3028(out);
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
  case RV3028: {
    const uint8_t r[7] = {
        toBcd((uint8_t)t.tm_sec),        toBcd((uint8_t)t.tm_min),
        toBcd((uint8_t)t.tm_hour),       // bit 6 clear selects 24 hour mode
        (uint8_t)t.tm_wday,              toBcd((uint8_t)t.tm_mday),
        toBcd((uint8_t)(t.tm_mon + 1)),  toBcd((uint8_t)(t.tm_year % 100)),
    };
    if (!writeRegisters(ADDR_RV3028, RV3028_TIME, r, 7)) {
      return false;
    }
    // The power-on reset flag has to be cleared by hand, or every later read
    // would go on reporting the time as unset. Read back rather than writing
    // a bare zero, so the chip's other status bits are left as they were.
    uint8_t status;
    if (!readRegisters(ADDR_RV3028, RV3028_STATUS, &status, 1)) {
      return false;
    }
    status &= (uint8_t)~RV3028_PORF;
    return writeRegisters(ADDR_RV3028, RV3028_STATUS, &status, 1);
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
