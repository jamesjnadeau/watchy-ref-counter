#pragma once

// Host stub for the ESP32 core's Wire library.
//
// Enough of an I2C bus to exercise RefRtc off the watch: devices answer at an
// address or they do not, and each one is a flat file of 256 registers with an
// auto-incrementing read pointer, which is how both RTC chips behave for the
// handful of registers this project touches.
//
// This is what lets the BCD decoding be tested at all. It is the piece of the
// firmware most likely to be wrong and least likely to be caught by eye: the
// PCF8563 holds the date at 0x05 and the weekday at 0x06, the RV-3028 the
// weekday at 0x03 and the date at 0x04, and a transposition there reads a
// plausible wrong date rather than failing loudly.
//
// What this stub does NOT model, so do not read a passing test as proof of it:
// clock stretching, bus arbitration, the RV-3028's 950ms I2C timeout, or a
// device that answers its address and then NACKs mid-transfer for any reason
// other than `nacks` below.

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace WireStub {

struct Device {
  uint8_t reg[256] = {};

  // Register pointer. It belongs to the device rather than to the bus, as it
  // does on the real parts, so addressing one chip cannot move another's.
  uint8_t pointer = 0;

  // Fail some upcoming transactions, so the driver's error paths can be
  // reached. `nackSkip` transactions are let through first, then `nacks` of
  // them are refused; both count down. A driver call is usually more than one
  // transaction -- reading a register is a write of the pointer followed by a
  // read -- so count them when aiming at a particular one.
  int nackSkip = 0;
  int nacks    = 0;

  // True when this transaction should be refused.
  bool refuse() {
    if (nacks <= 0) {
      return false;
    }
    if (nackSkip > 0) {
      nackSkip--;
      return false;
    }
    nacks--;
    return true;
  }
};

inline std::map<uint8_t, Device> &devices() {
  static std::map<uint8_t, Device> d;
  return d;
}

// Put a device on the bus at `address`, wiping any previous one.
inline Device &attach(uint8_t address) {
  devices().erase(address);
  return devices()[address];
}

// Empty the bus. Call between cases so one does not leak into the next.
inline void reset() { devices().clear(); }

inline Device *find(uint8_t address) {
  auto it = devices().find(address);
  return it == devices().end() ? nullptr : &it->second;
}

} // namespace WireStub

class TwoWire {
public:
  void begin() {}
  void begin(int, int) {}

  void beginTransmission(uint8_t address) {
    _address = address;
    _out.clear();
  }

  size_t write(uint8_t value) {
    _out.push_back(value);
    return 1;
  }

  size_t write(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
      _out.push_back(buf[i]);
    }
    return len;
  }

  // 0 on success, 2 for an address NACK -- which is what a probe of an empty
  // address looks like, and how RefRtc tells the two chips apart.
  uint8_t endTransmission() {
    WireStub::Device *dev = WireStub::find(_address);
    if (dev == nullptr) {
      return 2;
    }
    if (dev->refuse()) {
      return 2;
    }
    if (_out.empty()) {
      return 0; // bare probe: addressed, said nothing
    }
    dev->pointer = _out[0];
    for (size_t i = 1; i < _out.size(); i++) {
      dev->reg[(uint8_t)(dev->pointer + i - 1)] = _out[i];
    }
    return 0;
  }

  uint8_t requestFrom(uint8_t address, uint8_t quantity) {
    _in.clear();
    _cursor = 0;
    WireStub::Device *dev = WireStub::find(address);
    if (dev == nullptr) {
      return 0;
    }
    if (dev->refuse()) {
      return 0;
    }
    // Reads come off this device's own pointer, so a driver that set the
    // pointer on one address and read from another gets nothing useful.
    for (uint8_t i = 0; i < quantity; i++) {
      _in.push_back(dev->reg[(uint8_t)(dev->pointer + i)]);
    }
    return quantity;
  }

  int read() {
    if (_cursor >= _in.size()) {
      return -1;
    }
    return _in[_cursor++];
  }

private:
  uint8_t              _address = 0;
  std::vector<uint8_t> _out;
  std::vector<uint8_t> _in;
  size_t               _cursor = 0;
};

inline TwoWire Wire;
