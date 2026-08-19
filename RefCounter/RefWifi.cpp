#include "RefWifi.h"

#include <Preferences.h>

namespace RefWifi {
namespace {

// NVS. Its own namespace alongside "refzone" and "refsport", so clearing one
// setting never disturbs another.
const char *NVS_NAMESPACE = "refwifi";
const char *NVS_KEY_SETUP = "setup";

// Cached, so the menu can ask on every redraw without reopening NVS.
bool wasSetUp = false;

} // namespace

void begin() {
  wasSetUp = false;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) { // read only
    wasSetUp = prefs.getBool(NVS_KEY_SETUP, false);
    prefs.end();
  }
}

bool configured() { return wasSetUp; }

void setConfigured(bool on) {
  wasSetUp = on;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_SETUP, wasSetUp);
    prefs.end();
  }
}

} // namespace RefWifi
