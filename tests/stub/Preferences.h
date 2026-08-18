#pragma once
#include <cstdint>
#include <cstddef>
// Host stub: begin() fails, so RefZone falls back to the settings.h defaults.
class Preferences {
public:
  bool begin(const char *, bool = false) { return false; }
  void end() {}
  uint8_t getUChar(const char *, uint8_t d) { return d; }
  bool getBool(const char *, bool d) { return d; }
  size_t putUChar(const char *, uint8_t) { return 1; }
  size_t putBool(const char *, bool) { return 1; }
};
