#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// Host stub for the ESP32 core's NVS wrapper.
//
// By default begin() fails, so a module under test falls back to its
// settings.h defaults -- which is what the time zone tests rely on. A test
// that wants to exercise persistence calls PreferencesStub::enable(true), and
// clear() between cases so one does not leak into the next.
namespace PreferencesStub {

inline bool &enabledFlag() {
  static bool enabled = false;
  return enabled;
}
inline std::map<std::string, uint32_t> &store() {
  static std::map<std::string, uint32_t> s;
  return s;
}
inline void enable(bool on) { enabledFlag() = on; }
inline void clear() { store().clear(); }

} // namespace PreferencesStub

class Preferences {
public:
  bool begin(const char *ns, bool = false) {
    _ns = ns ? ns : "";
    return PreferencesStub::enabledFlag();
  }
  void end() {}

  uint8_t  getUChar(const char *k, uint8_t d)   { return (uint8_t)get(k, d); }
  uint16_t getUShort(const char *k, uint16_t d) { return (uint16_t)get(k, d); }
  bool     getBool(const char *k, bool d)       { return get(k, d ? 1 : 0) != 0; }

  size_t putUChar(const char *k, uint8_t v)   { return put(k, v); }
  size_t putUShort(const char *k, uint16_t v) { return put(k, v); }
  size_t putBool(const char *k, bool v)       { return put(k, v ? 1 : 0); }

private:
  std::string key(const char *k) const { return _ns + "/" + (k ? k : ""); }

  uint32_t get(const char *k, uint32_t fallback) const {
    const auto it = PreferencesStub::store().find(key(k));
    return it == PreferencesStub::store().end() ? fallback : it->second;
  }
  size_t put(const char *k, uint32_t v) {
    PreferencesStub::store()[key(k)] = v;
    return 1;
  }

  std::string _ns;
};
