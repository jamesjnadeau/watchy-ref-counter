// Host test for the "WiFi has been set up" flag in RefWifi.cpp. Compiles the
// real source against a stub Preferences, so what is under test is the shipped
// code. The flag is what decides whether the menu offers "Sync NTP" at all,
// so its default matters as much as its persistence: a watch that has never
// been through the setup screen must read false.
#include "RefWifi.h"

#include <Preferences.h>
#include <cstdio>

static int failures = 0;

static void expectBool(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-46s want %s got %s\n", what, want ? "true" : "false",
           got ? "true" : "false");
    failures++;
  } else {
    printf("ok   %-46s %s\n", what, got ? "true" : "false");
  }
}

int main() {
  // Storage unavailable: begin() must still leave a defined value, and it must
  // be false. A watch whose NVS will not open has no credentials either.
  PreferencesStub::enable(false);
  RefWifi::setConfigured(true); // write goes nowhere, but caches true
  RefWifi::begin();
  expectBool("no storage reads as not set up", RefWifi::configured(), false);

  // Storage available but empty: the same, by way of the default rather than
  // by way of a failed open.
  PreferencesStub::enable(true);
  PreferencesStub::clear();
  RefWifi::begin();
  expectBool("nothing stored reads as not set up", RefWifi::configured(), false);

  // Setting it takes effect immediately, without a begin() in between.
  RefWifi::setConfigured(true);
  expectBool("set true is visible at once", RefWifi::configured(), true);

  // ...and survives a reboot, which is the whole point of it being in NVS:
  // "Sync NTP" must still be there the next time the watch comes up.
  RefWifi::begin();
  expectBool("true survives restart", RefWifi::configured(), true);

  // Clearing it persists too. A failed run of Setup WiFi wipes the stored
  // credentials, so the row has to go away again.
  RefWifi::setConfigured(false);
  RefWifi::begin();
  expectBool("false survives restart", RefWifi::configured(), false);

  PreferencesStub::enable(false);
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
