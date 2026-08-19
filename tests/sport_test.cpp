// Host test for the sport preset table in RefSport.cpp. Compiles the real
// source against a stub Preferences, so what is under test is the shipped code.
#include "RefSport.h"
#include <Preferences.h>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expectEq(const char *what, long got, long want) {
  if (got != want) {
    printf("FAIL %-46s want %ld got %ld\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %ld\n", what, got);
  }
}

static void expectName(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s want %s got %s\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %s\n", what, got);
  }
}

// Find a preset by name, or -1.
static int find(const char *name) {
  for (uint8_t i = 0; i < RefSport::count(); i++) {
    if (strcmp(RefSport::preset(i).name, name) == 0) {
      return (int)i;
    }
  }
  printf("FAIL no such preset %s\n", name);
  failures++;
  return -1;
}

static void expectPreset(const char *name, uint16_t lng, uint16_t shrt,
                         uint16_t w1, uint16_t w2, uint16_t fin) {
  const int i = find(name);
  if (i < 0) {
    return;
  }
  const RefSport::Preset p = RefSport::preset((uint8_t)i);
  char what[80];
  snprintf(what, sizeof(what), "%s long", name);   expectEq(what, p.longSeconds, lng);
  snprintf(what, sizeof(what), "%s short", name);  expectEq(what, p.shortSeconds, shrt);
  snprintf(what, sizeof(what), "%s warn 1", name); expectEq(what, p.warnAtSeconds, w1);
  snprintf(what, sizeof(what), "%s warn 2", name); expectEq(what, p.warn2AtSeconds, w2);
  snprintf(what, sizeof(what), "%s final", name);  expectEq(what, p.finalCountdownFrom, fin);
}

// Menu rows are laid out on the assumption these stay short.
static void expectLabelWidths() {
  for (uint8_t i = 0; i < RefSport::count(); i++) {
    const RefSport::Preset p = RefSport::preset(i);
    char what[64];
    snprintf(what, sizeof(what), "%s name <= 9 chars", p.name);
    expectEq(what, (long)(strlen(p.name) <= 9), 1);
    snprintf(what, sizeof(what), "%s desc <= 14 chars", p.name);
    expectEq(what, (long)(strlen(p.description) <= 14), 1);
  }
}

int main() {
  // --- Nothing stored: the settings.h defaults apply ------------------------
  PreferencesStub::enable(false);
  RefSport::begin();

  expectEq("preset count", RefSport::count(), 7);
  expectName("default sport", RefSport::active().name, "Football");
  expectEq("Custom is the last entry",
           RefSport::isCustom((uint8_t)(RefSport::count() - 1)), 1);
  expectEq("Football is not Custom", RefSport::isCustom(0), 0);

  expectPreset("Football",  40, 25, 10,  0, 5);
  expectPreset("Lacrosse", 120, 20, 30, 10, 5);
  expectPreset("Base NCAA", 120, 20, 30, 10, 5);
  expectPreset("Base NFHS",  80, 20, 30, 10, 5);
  expectPreset("Soft NCAA",  90, 20, 30, 10, 5);
  expectPreset("Soft NFHS",  60, 20, 20, 10, 5);
  expectPreset("Custom",     40, 25, 10,  0, 5);
  expectLabelWidths();

  // An out of range index falls back to the first preset rather than reading
  // off the end of the table.
  expectName("index 200 clamps", RefSport::preset(200).name, "Football");

  // active() follows setIndex even with no storage behind it.
  RefSport::setIndex((uint8_t)find("Lacrosse"));
  expectEq("active long after select", RefSport::active().longSeconds, 120);
  expectEq("active warn 2 after select", RefSport::active().warn2AtSeconds, 10);

  // --- Clamping ------------------------------------------------------------
  RefSport::setCustom(0, 200, 500, 0, 250);
  expectEq("custom long clamps up to 1", RefSport::custom().longSeconds, 1);
  expectEq("custom short clamps to 199", RefSport::custom().shortSeconds, 199);
  expectEq("custom warn 1 clamps to 199", RefSport::custom().warnAtSeconds, 199);
  expectEq("custom warn 2 zero is allowed", RefSport::custom().warn2AtSeconds, 0);
  expectEq("custom final clamps to 199", RefSport::custom().finalCountdownFrom, 199);

  // --- With storage: the choice survives a restart -------------------------
  PreferencesStub::enable(true);
  PreferencesStub::clear();
  RefSport::begin();
  expectName("fresh NVS still defaults", RefSport::active().name, "Football");

  RefSport::setIndex((uint8_t)find("Soft NFHS"));
  RefSport::setCustom(90, 30, 20, 8, 3);

  RefSport::begin(); // as if the watch rebooted
  expectName("sport survives restart", RefSport::active().name, "Soft NFHS");
  expectEq("custom long survives restart", RefSport::custom().longSeconds, 90);
  expectEq("custom short survives restart", RefSport::custom().shortSeconds, 30);
  expectEq("custom warn 1 survives restart", RefSport::custom().warnAtSeconds, 20);
  expectEq("custom warn 2 survives restart", RefSport::custom().warn2AtSeconds, 8);
  expectEq("custom final survives restart", RefSport::custom().finalCountdownFrom, 3);

  // Custom selected: active() reports the edited numbers, not the factory ones.
  RefSport::setIndex((uint8_t)find("Custom"));
  RefSport::begin();
  expectName("custom survives restart as active", RefSport::active().name, "Custom");
  expectEq("active reads edited custom", RefSport::active().longSeconds, 90);

  // A garbage index in NVS must not select off the end of the table.
  Preferences prefs;
  prefs.begin("refsport", false);
  prefs.putUChar("sport", 200);
  prefs.end();
  RefSport::begin();
  expectName("garbage index clamps", RefSport::active().name, "Football");

  // Hand-edited Custom numbers in NVS must clamp on the way in too, not only
  // when they arrive through setCustom(). begin() reads five keys off the wire
  // (clong, cshort, cwarn, cwarn2, cfinal); a value planted directly through
  // the Preferences API, bypassing setCustom entirely, is the case that
  // exercises that read path rather than the write path.
  Preferences nvsPrefs;
  nvsPrefs.begin("refsport", false);
  nvsPrefs.putUShort("clong", 0);    // below the clock floor of 1
  nvsPrefs.putUShort("cwarn", 9000); // above the 199 ceiling
  nvsPrefs.end();
  RefSport::begin();
  expectEq("NVS clock below floor clamps to 1",
           RefSport::custom().longSeconds, 1);
  expectEq("NVS mark above ceiling clamps to 199",
           RefSport::custom().warnAtSeconds, 199);

  PreferencesStub::enable(false);
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
