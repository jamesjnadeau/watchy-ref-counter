# Settings Menu Order and the WiFi Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorder the settings menu to About, Sync NTP, TZ, DST, Set Time, Setup WiFi, Sport, Edit Custom; hide `Sync NTP` until WiFi credentials have been saved; and delete the `Vibrate Motor` row.

**Architecture:** Two new host-testable translation units carved out of what is otherwise watch-only code. `RefWifi` owns a single "credentials have been saved" flag in NVS, following the `RefZone`/`RefSport` persistence shape exactly. `RefMenuItems` owns the menu's row order, the visibility rule and the row labels, so the ordering can be tested on a host — `RefMenu.cpp` itself pulls in GxEPD2, WiFiManager and GPIO and cannot be compiled off the watch. `RefMenu.cpp` keeps only drawing and button handling, and its highlight index becomes a *slot* into a rebuilt list of visible rows rather than an `Item` id.

**Tech Stack:** C++11 (Arduino ESP32 core), GxEPD2, Adafruit GFX, WiFiManager, ESP32 `Preferences` (NVS). Host tests are plain g++ against `tests/stub/Preferences.h`.

**Spec:** [`docs/superpowers/specs/2026-08-18-menu-order-and-wifi-gate.md`](../specs/2026-08-18-menu-order-and-wifi-gate.md)

## Global Constraints

- **No new libraries.** GxEPD2, Adafruit GFX and WiFiManager stay the only three.
- **Final menu order is fixed:** `About`, `Sync NTP`, `TZ: <name>`, `DST: Auto/Off`, `Set Time`, `Setup WiFi`, `Sport: <name>`, `Edit Custom`. `Sync NTP` is the only conditional row.
- **`MENU_VISIBLE` stays 7.** Eight rows scroll; seven do not.
- **Menu labels are 18 glyphs or fewer.** `FreeMonoBold9pt7b` advances 11 pixels a glyph on a 200 pixel panel, so 18 glyphs is 198 pixels. The label buffer stays 24 bytes.
- **New persistence follows the `RefZone` pattern:** its own NVS namespace, a `Preferences` handle opened and closed per read/write, the value cached in a file-static so reads cost nothing, defaults applied when nothing is stored.
- **`RefWifi.cpp` and `RefMenuItems.cpp` must compile on a host** with no Arduino headers beyond the `Preferences` stub. Neither may include `RefPanel.h`, `board.h`, `WiFi.h`, `WiFiManager.h` or anything from GxEPD2.
- **Nothing in the menu may power up the WiFi radio to decide what to draw.** The gate reads a cached bool, never the ESP32's stored STA config.
- **Every task ends with all four PlatformIO envs building:** `watchy_v2`, `watchy_v15`, `watchy_v10`, `watchy_v3`.
- **Nothing here can be verified on hardware.** The repo has never run on a watch (README *Known limitations*). "Verify" means the host tests pass and all four envs compile; do not claim otherwise in commit messages.

## File Structure

| File | Responsibility |
| --- | --- |
| `RefCounter/RefWifi.h` / `.cpp` | **new.** The "WiFi has been set up" flag: `begin()`, `configured()`, `setConfigured()`. Pure NVS, no radio. |
| `RefCounter/RefMenuItems.h` / `.cpp` | **new.** `enum Item`, `buildVisible()`, `slotOf()`, `itemLabel()`. No drawing, no buttons, no Arduino headers. |
| `RefCounter/RefMenu.cpp` | **modified.** Loses the enum, `itemLabel` and `showBuzz`; gains a visible-row list and slot-based navigation. |
| `RefCounter/RefCounter.ino` | **modified.** One `RefWifi::begin()` call in `setup()`. |
| `tests/wifi_test.cpp` | **new.** The flag's default, its persistence across a simulated reboot, and the storage-unavailable fallback. |
| `tests/menu_items_test.cpp` | **new.** Both row orders, the slot shift when `Sync NTP` appears, the label widths, and that `Vibrate Motor` is gone. |
| `tests/run.sh` | **modified.** Two more suites. |
| `README.md` | **modified.** Menu table, row count, the upgrade note about re-running Setup WiFi, source tree, testing section. |

---

### Task 1: The WiFi-configured flag

A one-value persistence module. It goes first because `RefMenuItems` and `RefMenu` both depend on it, and because it is the only piece with a user-visible upgrade consequence worth pinning down in a test.

**Files:**
- Create: `RefCounter/RefWifi.h`
- Create: `RefCounter/RefWifi.cpp`
- Create: `tests/wifi_test.cpp`
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `void RefWifi::begin()` — load the flag from NVS; nothing stored means false.
  - `bool RefWifi::configured()` — the cached flag.
  - `void RefWifi::setConfigured(bool on)` — set and persist immediately.

- [ ] **Step 1: Write the failing test**

Create `tests/wifi_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Add the suite to the test runner**

In `tests/run.sh`, add the entry to the `SOURCES` map:

```bash
declare -A SOURCES=(
  [tz_test]="RefCounter/RefZone.cpp"
  [tz_edges]="RefCounter/RefZone.cpp"
  [segments_test]="RefCounter/RefSegments.cpp"
  [sport_test]="RefCounter/RefSport.cpp"
  [wifi_test]="RefCounter/RefWifi.cpp"
)
```

and to the loop:

```bash
for t in tz_test tz_edges segments_test sport_test wifi_test; do
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `./tests/run.sh`
Expected: FAIL — the compile of `wifi_test` errors with `RefWifi.h: No such file or directory`. The four existing suites still pass first.

- [ ] **Step 4: Write the header**

Create `RefCounter/RefWifi.h`:

```cpp
#ifndef REF_WIFI_H
#define REF_WIFI_H

// Whether the menu's "Setup WiFi" screen has ever saved working credentials.
//
// Nothing here touches the radio. The flag is a single bool in NVS, written by
// the setup screen and read by the menu, so deciding whether to offer "Sync
// NTP" costs nothing -- asking the ESP32 for its stored SSID instead would
// mean powering the WiFi driver up every time the menu drew a row.
//
// The flag is cleared as well as set. "Setup WiFi" wipes the saved credentials
// before it raises its access point, so a setup that fails or times out really
// has left the watch with nothing to connect to, and the row it gates has to
// go away with them.
namespace RefWifi {

// Load the saved flag out of NVS. Nothing stored means "never set up", which
// is also what a watch upgraded from a firmware without this flag reads --
// running "Setup WiFi" once more is what brings "Sync NTP" back.
// Call once at startup.
void begin();

bool configured();
void setConfigured(bool on); // persists immediately

} // namespace RefWifi

#endif // REF_WIFI_H
```

- [ ] **Step 5: Write the implementation**

Create `RefCounter/RefWifi.cpp`:

```cpp
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `./tests/run.sh`
Expected: five `PASSED` blocks, including `=== wifi_test` with five `ok` lines.

- [ ] **Step 7: Commit**

```bash
git add RefCounter/RefWifi.h RefCounter/RefWifi.cpp tests/wifi_test.cpp tests/run.sh
git commit -m "feat: remember whether WiFi has ever been set up"
```

---

### Task 2: The menu's row list

Pull the menu's order, its labels and the new visibility rule out of `RefMenu.cpp` into a pure translation unit, in the new order and without the buzz row. `RefMenu.cpp` is not touched yet — this task adds the module and its test, and the next one switches the menu over to it. Splitting there is deliberate: this half is testable on a host and the next half is not, so a reviewer can accept the ordering logic on evidence before reading the display code.

**Files:**
- Create: `RefCounter/RefMenuItems.h`
- Create: `RefCounter/RefMenuItems.cpp`
- Create: `tests/menu_items_test.cpp`
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: `RefSport::active()`, `RefZone::name()`, `RefZone::index()`, `RefZone::dstAuto()` — all already shipped.
- Produces:
  - `enum RefMenu::Item : uint8_t { ITEM_ABOUT, ITEM_SYNC, ITEM_ZONE, ITEM_DST, ITEM_SET_TIME, ITEM_WIFI, ITEM_SPORT, ITEM_EDIT_CUSTOM, ITEM_COUNT }`
  - `static const size_t RefMenu::ITEM_LABEL_MAX = 24`
  - `uint8_t RefMenu::buildVisible(bool wifiConfigured, uint8_t *out)` — fills `out[0..n)` with item ids in menu order, returns `n`. `out` must hold `ITEM_COUNT` entries.
  - `int8_t RefMenu::slotOf(const uint8_t *visible, uint8_t count, uint8_t item)` — position of `item` in that list, or `-1`.
  - `void RefMenu::itemLabel(uint8_t item, char *buf, size_t n)`

- [ ] **Step 1: Write the failing test**

Create `tests/menu_items_test.cpp`:

```cpp
// Host test for the settings menu's row list in RefMenuItems.cpp. Compiles the
// real source, so the order under test is the shipped order.
//
// RefMenu.cpp itself cannot be built here -- GxEPD2, WiFiManager and GPIO --
// which is exactly why the list lives in its own file: the order, the
// conditional "Sync NTP" row and the slot arithmetic behind them are cheap to
// check here and expensive to check on a watch.
#include "RefMenuItems.h"

#include "RefSport.h"
#include "RefZone.h"

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

static void expectStr(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s want %s got %s\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %s\n", what, got);
  }
}

// The order the list must come back in, with and without the WiFi row.
static const uint8_t WANT_NO_WIFI[] = {
    RefMenu::ITEM_ABOUT, RefMenu::ITEM_ZONE,  RefMenu::ITEM_DST,
    RefMenu::ITEM_SET_TIME, RefMenu::ITEM_WIFI, RefMenu::ITEM_SPORT,
    RefMenu::ITEM_EDIT_CUSTOM};

static const uint8_t WANT_WITH_WIFI[] = {
    RefMenu::ITEM_ABOUT, RefMenu::ITEM_SYNC, RefMenu::ITEM_ZONE,
    RefMenu::ITEM_DST,   RefMenu::ITEM_SET_TIME, RefMenu::ITEM_WIFI,
    RefMenu::ITEM_SPORT, RefMenu::ITEM_EDIT_CUSTOM};

static void expectOrder(const char *what, const uint8_t *got, uint8_t gotCount,
                        const uint8_t *want, uint8_t wantCount) {
  char label[80];
  snprintf(label, sizeof(label), "%s row count", what);
  expectEq(label, gotCount, wantCount);
  const uint8_t n = gotCount < wantCount ? gotCount : wantCount;
  for (uint8_t i = 0; i < n; i++) {
    snprintf(label, sizeof(label), "%s slot %u", what, (unsigned)i);
    expectEq(label, got[i], want[i]);
  }
}

int main() {
  // Nothing stored, so the labels that read a value read the settings.h
  // defaults: Eastern, DST auto, Football.
  PreferencesStub::enable(false);
  RefZone::begin();
  RefSport::begin();

  uint8_t visible[RefMenu::ITEM_COUNT];

  // WiFi never set up: seven rows and no Sync NTP, because with no credentials
  // stored that row could only ever fail.
  uint8_t n = RefMenu::buildVisible(false, visible);
  expectOrder("no wifi", visible, n, WANT_NO_WIFI,
              (uint8_t)(sizeof(WANT_NO_WIFI) / sizeof(WANT_NO_WIFI[0])));
  expectEq("sync absent without wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC), -1);
  expectEq("setup wifi sits at slot 4",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_WIFI), 4);

  // WiFi set up: eight rows, Sync NTP second.
  n = RefMenu::buildVisible(true, visible);
  expectOrder("with wifi", visible, n, WANT_WITH_WIFI,
              (uint8_t)(sizeof(WANT_WITH_WIFI) / sizeof(WANT_WITH_WIFI[0])));
  expectEq("sync is second with wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC), 1);
  // The case the menu depends on: running Setup WiFi inserts a row *above* it,
  // so the highlight has to be moved by item, not left on slot 4.
  expectEq("setup wifi shifts down one",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_WIFI), 5);
  expectEq("unknown item has no slot",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_COUNT), -1);

  // Labels. Fixed text where it is fixed, and the three value-carrying rows
  // reading their defaults.
  char label[RefMenu::ITEM_LABEL_MAX];
  RefMenu::itemLabel(RefMenu::ITEM_ABOUT, label, sizeof(label));
  expectStr("about label", label, "About");
  RefMenu::itemLabel(RefMenu::ITEM_SYNC, label, sizeof(label));
  expectStr("sync label", label, "Sync NTP");
  RefMenu::itemLabel(RefMenu::ITEM_SET_TIME, label, sizeof(label));
  expectStr("set time label", label, "Set Time");
  RefMenu::itemLabel(RefMenu::ITEM_WIFI, label, sizeof(label));
  expectStr("setup wifi label", label, "Setup WiFi");
  RefMenu::itemLabel(RefMenu::ITEM_EDIT_CUSTOM, label, sizeof(label));
  expectStr("edit custom label", label, "Edit Custom");
  RefMenu::itemLabel(RefMenu::ITEM_ZONE, label, sizeof(label));
  expectStr("zone label reads the zone", label, "TZ: Eastern");
  RefMenu::itemLabel(RefMenu::ITEM_DST, label, sizeof(label));
  expectStr("dst label reads the switch", label, "DST: Auto");
  RefMenu::itemLabel(RefMenu::ITEM_SPORT, label, sizeof(label));
  expectStr("sport label reads the sport", label, "Sport: Football");

  // Every row has to fit the panel: FreeMonoBold9pt7b advances 11 pixels a
  // glyph and the panel is 200 wide, so 18 glyphs is the ceiling. The buzz
  // test is gone, and must not creep back in.
  for (uint8_t i = 0; i < RefMenu::ITEM_COUNT; i++) {
    RefMenu::itemLabel(i, label, sizeof(label));
    char what[80];
    snprintf(what, sizeof(what), "%s fits 18 glyphs", label);
    expectEq(what, (long)strlen(label) <= 18, 1);
    snprintf(what, sizeof(what), "%s is not the buzz test", label);
    expectEq(what, strcmp(label, "Vibrate Motor") != 0, 1);
  }

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
```

- [ ] **Step 2: Add the suite to the test runner**

In `tests/run.sh`, add the entry to the `SOURCES` map — three sources, since the value-carrying labels read the real sport and zone tables:

```bash
  [menu_items_test]="RefCounter/RefMenuItems.cpp RefCounter/RefSport.cpp RefCounter/RefZone.cpp"
```

and to the loop:

```bash
for t in tz_test tz_edges segments_test sport_test wifi_test menu_items_test; do
```

Also update the comment at the top of `tests/run.sh`, which currently names only three testable files:

```bash
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefWifi.cpp and RefMenuItems.cpp
# are the files with logic worth testing off the watch: no panel, no GPIO, and
# the sort of bugs -- a daylight saving date, a digit running off the edge of
# the screen, a clamp, a menu row in the wrong place -- that are expensive to
# find on hardware. They compile here against a stub Preferences; everything
# else is the shipped source.
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `./tests/run.sh`
Expected: FAIL — the compile of `menu_items_test` errors with `RefMenuItems.h: No such file or directory`. The five earlier suites still pass.

- [ ] **Step 4: Write the header**

Create `RefCounter/RefMenuItems.h`:

```cpp
#ifndef REF_MENU_ITEMS_H
#define REF_MENU_ITEMS_H

#include <stddef.h>
#include <stdint.h>

// The settings menu's row list: which rows exist, what order they come in, and
// what each one says.
//
// Split out of RefMenu.cpp so it can be compiled and tested on a host --
// RefMenu.cpp pulls in GxEPD2, WiFiManager and the GPIO layer, none of which
// build off the watch. Nothing here draws anything or reads a button.
namespace RefMenu {

// Menu order. About first, because that is the row the menu opens on, then the
// three rows that touch the clock and the two that touch the radio. The sport
// rows are last: they are set once for the season, where everything above them
// is either read or adjusted in the field.
enum Item : uint8_t {
  ITEM_ABOUT,
  ITEM_SYNC,
  ITEM_ZONE,
  ITEM_DST,
  ITEM_SET_TIME,
  ITEM_WIFI,
  ITEM_SPORT,
  ITEM_EDIT_CUSTOM,
  ITEM_COUNT,
};

// The widest label is "Sport: Base NCAA" at 16 glyphs, and the panel fits 18.
// 24 leaves room rather than sizing the buffer to the exact width and hoping
// nothing grows past it.
static const size_t ITEM_LABEL_MAX = 24;

// Fill `out` -- which must hold ITEM_COUNT entries -- with the items to show,
// in menu order, and return how many there are. "Sync NTP" is left out until
// WiFi credentials have been saved: with none stored it could only ever fail.
uint8_t buildVisible(bool wifiConfigured, uint8_t *out);

// Which slot of a list built by buildVisible holds `item`, or -1 when that
// item is not currently shown. This is how the highlight follows a row across
// a rebuild: running "Setup WiFi" can make "Sync NTP" appear *above* it, which
// moves it and every row below it down one.
int8_t slotOf(const uint8_t *visible, uint8_t count, uint8_t item);

// The row's text. Three rows read their current value rather than being fixed
// text, so the menu doubles as the status display for the sport, the zone and
// the DST switch.
void itemLabel(uint8_t item, char *buf, size_t n);

} // namespace RefMenu

#endif // REF_MENU_ITEMS_H
```

- [ ] **Step 5: Write the implementation**

Create `RefCounter/RefMenuItems.cpp`:

```cpp
#include "RefMenuItems.h"

#include <stdio.h>

#include "RefSport.h"
#include "RefZone.h"

namespace RefMenu {

uint8_t buildVisible(bool wifiConfigured, uint8_t *out) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    if (i == ITEM_SYNC && !wifiConfigured) {
      continue;
    }
    out[count++] = i;
  }
  return count;
}

int8_t slotOf(const uint8_t *visible, uint8_t count, uint8_t item) {
  for (uint8_t s = 0; s < count; s++) {
    if (visible[s] == item) {
      return (int8_t)s;
    }
  }
  return -1;
}

void itemLabel(uint8_t item, char *buf, size_t n) {
  switch (item) {
  case ITEM_ABOUT:    snprintf(buf, n, "About"); break;
  case ITEM_SYNC:     snprintf(buf, n, "Sync NTP"); break;
  case ITEM_ZONE:     snprintf(buf, n, "TZ: %s", RefZone::name(RefZone::index())); break;
  case ITEM_DST:      snprintf(buf, n, "DST: %s", RefZone::dstAuto() ? "Auto" : "Off"); break;
  case ITEM_SET_TIME: snprintf(buf, n, "Set Time"); break;
  case ITEM_WIFI:     snprintf(buf, n, "Setup WiFi"); break;
  case ITEM_SPORT:    snprintf(buf, n, "Sport: %s", RefSport::active().name); break;
  case ITEM_EDIT_CUSTOM: snprintf(buf, n, "Edit Custom"); break;
  default:            snprintf(buf, n, "?"); break;
  }
}

} // namespace RefMenu
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `./tests/run.sh`
Expected: six `PASSED` blocks. `=== menu_items_test` reports both orders, both `Setup WiFi` slots (4 without the sync row, 5 with it), the eight labels, and 16 width/no-buzz checks.

- [ ] **Step 7: Commit**

```bash
git add RefCounter/RefMenuItems.h RefCounter/RefMenuItems.cpp tests/menu_items_test.cpp tests/run.sh
git commit -m "feat: give the menu's row list its own host-tested module"
```

---

### Task 3: Switch the menu over

Wire `RefMenu.cpp` to the new list, delete the buzz test, write the flag from the setup screen, call `RefWifi::begin()` at startup, and bring the README in line. One task because none of it compiles alone: `RefMenu.cpp` stops declaring its own `Item` enum in the same edit that starts using the header's.

Nothing here is host-testable — `RefMenu.cpp` needs GxEPD2, WiFiManager and GPIO. Verification is the six host suites still passing, all four PlatformIO envs compiling, and greps confirming the deleted code really is gone.

**Files:**
- Modify: `RefCounter/RefMenu.cpp`
- Modify: `RefCounter/RefCounter.ino:283-296` (`setup()`)
- Modify: `README.md`

**Interfaces:**
- Consumes: everything Task 1 and Task 2 produced — `RefWifi::begin/configured/setConfigured`, `RefMenu::Item`, `RefMenu::ITEM_LABEL_MAX`, `RefMenu::buildVisible`, `RefMenu::slotOf`, `RefMenu::itemLabel`.
- Produces: no new API. `RefMenu::open(RefClock &)` keeps its signature.

- [ ] **Step 1: Add the two new includes**

In `RefCounter/RefMenu.cpp`, replace the project include block:

```cpp
#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "RefPanel.h"
#include "RefSport.h"
#include "RefZone.h"
#include "board.h"
#include "settings.h"
```

with:

```cpp
#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "RefMenuItems.h"
#include "RefPanel.h"
#include "RefSport.h"
#include "RefWifi.h"
#include "RefZone.h"
#include "board.h"
#include "settings.h"
```

`RefSport.h` and `RefZone.h` stay: the pickers and the DST toggle still use them directly.

- [ ] **Step 2: Delete the enum and `itemLabel`, and add the visible-row list**

In `RefCounter/RefMenu.cpp`, replace everything from the `// Three of these read their current value` comment down to the closing brace of `itemLabel` — the enum, `MENU_ROW_H`, `MENU_VISIBLE`, `menuTop` and `itemLabel` — with:

```cpp
const int16_t MENU_ROW_H = 25;

// Rows that fit on the panel at MENU_ROW_H. With WiFi set up the list is one
// row longer than this, so drawMenu scrolls a window over it, the way
// pickTimeZone does over the zones; without it the list fits exactly.
const uint8_t MENU_VISIBLE = 7;

// The rows currently on offer, in order, and how many there are. Rebuilt on
// the way in and after every action, because "Setup WiFi" can add or remove
// "Sync NTP" underneath the user.
uint8_t visible[ITEM_COUNT];
uint8_t visibleCount = 0;

// Top of the visible window. Lives outside drawMenu so it survives the redraws
// that follow an action, and is reset in open() so the menu never comes up
// scrolled from last time.
uint8_t menuTop = 0;
```

The order, the labels and the visibility rule now live in `RefMenuItems.cpp`. These are all inside `namespace RefMenu`, so `ITEM_COUNT`, `itemLabel`, `buildVisible` and `slotOf` resolve unqualified.

- [ ] **Step 3: Make `drawMenu` take a slot rather than an item**

Replace the whole of `drawMenu` with:

```cpp
// `slot` is a position in `visible`, not an Item id.
void drawMenu(uint8_t slot, bool partial) {
  // Keep the highlighted row inside the window, and never scroll past the end.
  if (slot < menuTop) {
    menuTop = slot;
  } else if (slot >= menuTop + MENU_VISIBLE) {
    menuTop = (uint8_t)(slot - MENU_VISIBLE + 1);
  }
  if (visibleCount > MENU_VISIBLE &&
      menuTop > (uint8_t)(visibleCount - MENU_VISIBLE)) {
    menuTop = (uint8_t)(visibleCount - MENU_VISIBLE);
  } else if (visibleCount <= MENU_VISIBLE) {
    menuTop = 0;
  }

  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);

  char label[ITEM_LABEL_MAX];
  int16_t x1, y1;
  uint16_t w, h;
  for (uint8_t row = 0; row < MENU_VISIBLE && menuTop + row < visibleCount;
       row++) {
    const uint8_t s = (uint8_t)(menuTop + row);
    itemLabel(visible[s], label, sizeof(label));
    const int16_t yPos = MENU_ROW_H + (MENU_ROW_H * row);
    display.setCursor(0, yPos);
    if (s == slot) {
      display.getTextBounds(label, 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, DISPLAY_WIDTH, h + 15, THEME_FG);
      display.setTextColor(THEME_BG);
    } else {
      display.setTextColor(THEME_FG);
    }
    display.println(label);
  }
  display.display(partial);
}
```

- [ ] **Step 4: Delete the buzz screen**

Remove `showBuzz` from `RefCounter/RefMenu.cpp` entirely:

```cpp
void showBuzz() {
  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(THEME_FG);
  display.setCursor(70, 80);
  display.println("Buzz!");
  display.display(false); // full refresh
  Buzzer::pulse(3, BUZZ_SHORT_MS, BUZZ_GAP_MS);
}
```

Leave `#include "Buzzer.h"` in place — the confirmation pulse after the sport picker, the zone picker, the Custom editor and the DST toggle all still use it.

- [ ] **Step 5: Record the outcome of WiFi setup**

Replace the body of `setupWifi` with:

```cpp
void setupWifi() {
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(WIFI_AP_TIMEOUT_S);
  wifiManager.setAPCallback(portalCallback);

  beginTextScreen();
  // resetSettings() above wiped whatever was stored, so the flag has to track
  // the outcome both ways: a setup that fails or times out really has left the
  // watch with no credentials, and "Sync NTP" goes away again with them.
  const bool ok = wifiManager.autoConnect(WIFI_AP_NAME);
  RefWifi::setConfigured(ok);
  if (!ok) {
    display.println("Setup failed or");
    display.println("timed out.");
  } else {
    display.println("Connected to:");
    display.println(WiFi.SSID());
    display.println("Local IP:");
    display.println(WiFi.localIP());
  }
  display.display(false); // full refresh

  WiFi.mode(WIFI_OFF);
}
```

- [ ] **Step 6: Rewrite `open()` to navigate by slot**

Replace `open()` down to the end of its `while` loop with:

```cpp
void open(RefClock &refClock) {
  // The hold that opened the menu is still down; without this it would land as
  // an immediate selection of the first entry.
  Buttons::waitForRelease();

  visibleCount = buildVisible(RefWifi::configured(), visible);
  uint8_t slot = 0;
  menuTop = 0;
  drawMenu(slot, false);
  claimButtons();

  uint32_t lastActivity = millis();
  while (millis() - lastActivity < MENU_TIMEOUT_MS) {
    if (pressed(PIN_BTN_MENU)) {
      Buttons::waitForRelease();
      const uint8_t item = visible[slot];
      bool holdResult = false;
      // The DST switch never leaves the menu, so it gets the quick partial
      // redraw rather than the 2.6s full one every other entry earns.
      bool quickRedraw = false;
      switch (item) {
      case ITEM_ABOUT:    showAbout(refClock); holdResult = true; break;
      case ITEM_SYNC:     showSyncNTP(refClock); break;
      case ITEM_ZONE:     pickTimeZone(); break;
      case ITEM_DST:
        RefZone::setDstAuto(!RefZone::dstAuto());
        Buzzer::pulse(BUZZ_CONFIRM_MS);
        quickRedraw = true;
        break;
      case ITEM_SET_TIME: setTime(refClock); break;
      case ITEM_WIFI:     setupWifi(); holdResult = true; break;
      case ITEM_SPORT:       pickSport(); break;
      case ITEM_EDIT_CUSTOM: editCustom(); break;
      default: break;
      }
      if (holdResult) {
        waitForBack();
      }
      Buttons::waitForRelease();
      claimButtons();
      // Setup WiFi can make "Sync NTP" appear above it, which moves that row
      // and every row below it down a slot, so the highlight follows the item
      // the user just ran rather than the slot number it used to sit at.
      visibleCount = buildVisible(RefWifi::configured(), visible);
      const int8_t back = slotOf(visible, visibleCount, item);
      slot = back >= 0 ? (uint8_t)back : 0;
      drawMenu(slot, quickRedraw);
      lastActivity = millis();
    } else if (pressed(PIN_BTN_BACK)) {
      break; // out of the menu entirely
    } else if (pressed(PIN_BTN_UP)) {
      slot = (slot == 0) ? (uint8_t)(visibleCount - 1) : (uint8_t)(slot - 1);
      drawMenu(slot, true);
      lastActivity = millis();
    } else if (pressed(PIN_BTN_DOWN)) {
      slot = (uint8_t)((slot + 1) % visibleCount);
      drawMenu(slot, true);
      lastActivity = millis();
    }
  }

  Buttons::waitForRelease();
  // The loop above read the pins raw. Re-seed the debounce state so a button
  // still settling is not mistaken for a fresh press by the main loop.
  Buttons::resync();
}
```

`visibleCount` is never 0 — `About` is unconditional — so the `%` and the `visibleCount - 1` wrap are both safe.

- [ ] **Step 7: Update the file's header comment**

In `RefCounter/RefMenu.cpp`, the "Deliberate differences from the reference" list still claims a buzz screen this file no longer has. Replace:

```cpp
// Deliberate differences from the reference:
//   - no "Show Accelerometer"; nothing on a play clock reads the sensor
```

with:

```cpp
// Deliberate differences from the reference:
//   - no "Show Accelerometer"; nothing on a play clock reads the sensor
//   - no "Vibrate Motor" buzz test; the buzzer proves itself every countdown
//   - "Sync NTP" is only listed once WiFi credentials have been saved, since
//     with none stored it could only ever report a failure
```

The opening paragraph of that comment stays as it is: the WiFi access point and the NTP sync screens are both still here, they are just reached from a different row.

- [ ] **Step 8: Load the flag at startup**

In `RefCounter/RefCounter.ino`, add the include alongside the others:

```cpp
#include "RefSport.h"
#include "RefWifi.h"
```

and the call in `setup()`, next to the other `begin()`s:

```cpp
  Buzzer::begin();
  Buttons::begin();
  RefSport::begin();
  RefWifi::begin();
  RefDisplay::begin();
```

- [ ] **Step 9: Verify the removed code is really gone**

Run:

```bash
grep -rn "ITEM_BUZZ\|showBuzz\|Vibrate Motor" RefCounter/ tests/
```

Expected: no output at all.

Run:

```bash
grep -n "RefWifi::\|buildVisible\|slotOf" RefCounter/RefMenu.cpp RefCounter/RefCounter.ino
```

Expected: `RefWifi::configured()` twice and `buildVisible` twice in `open()`, `RefWifi::setConfigured` once in `setupWifi`, `slotOf` once, and `RefWifi::begin()` once in the sketch.

- [ ] **Step 10: Run the host tests**

Run: `./tests/run.sh`
Expected: six `PASSED` blocks. Nothing in this task changes what they test, so a failure here means an edit landed in the wrong file.

- [ ] **Step 11: Build all four environments**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines. With WiFi set up `visibleCount` is 8 against a `MENU_VISIBLE` of 7, so `drawMenu` scrolls for real — the row that scrolls off the bottom is `Edit Custom`, reachable by pressing DOWN past `Sport`.

- [ ] **Step 12: Update the README**

Four edits in `README.md`.

**a.** The menu row count, around line 183. Replace:

```markdown
cannot strand you mid-game. Nine entries no longer fit the panel at once, so
the list scrolls a seven-row window, keeping the highlighted entry in view the
same way the time zone picker already does.
```

with:

```markdown
cannot strand you mid-game. Eight entries do not fit the panel at once, so the
list scrolls a seven-row window, keeping the highlighted entry in view the same
way the time zone picker already does. Before WiFi has been set up there are
only seven and it does not scroll at all.
```

**b.** The entry table, around line 196. Replace the whole table and the
paragraph under it with:

```markdown
| Entry | What it does |
| --- | --- |
| About | Version, board revision, battery, time, uptime, last sync, RTC type |
| Sync NTP | Connect and set the clock from `NTP_SERVER`. Only listed once **Setup WiFi** has connected |
| TZ: *name* | Pick a US time zone from a scrolling list |
| DST: Auto/Off | Turn the daylight saving rule on or off |
| Set Time | Set the clock by hand, no WiFi needed |
| Setup WiFi | Captive portal to save credentials |
| Sport: *name* | Pick one of the seven presets from a scrolling list |
| Edit Custom | Set the five Custom fields by hand |

The **Sport**, **TZ** and **DST** rows show their current value rather than
fixed text, so the menu doubles as the status display for all three. **DST**
toggles in place and redraws with a fast partial refresh; everything else
opens a screen.

**Sync NTP** is hidden until **Setup WiFi** has connected successfully, since
with no credentials stored it could only ever report a failure. The watch
remembers that in NVS (`RefWifi.cpp`), so it survives a reflash — but a watch
upgraded from a firmware without that flag has nothing stored and will hide the
row until **Setup WiFi** is run once more. A setup that fails or times out
clears the flag again, correctly: that screen wipes the saved credentials
before it raises its access point.
```

**c.** The "no Show Accelerometer" paragraph, just below. Replace:

```markdown
There is no "Show Accelerometer". The reference has one, but nothing on a play
clock reads the sensor, and leaving it out means the BMA423 is never powered
up at all.
```

with:

```markdown
There is no "Show Accelerometer". The reference has one, but nothing on a play
clock reads the sensor, and leaving it out means the BMA423 is never powered
up at all. There is no "Vibrate Motor" buzz test either — the buzzer announces
every warning mark and every expiry, so a dedicated screen for it earned a menu
row it did not need.
```

**d.** The source tree, around line 309, and the testing section, around line
513. In the tree, replace:

```markdown
    ├── RefSport.h/.cpp     sport presets and the custom slot
    └── RefMenu.h/.cpp     the settings menu
```

with:

```markdown
    ├── RefSport.h/.cpp     sport presets and the custom slot
    ├── RefWifi.h/.cpp      whether WiFi has ever been set up, host-tested
    ├── RefMenuItems.h/.cpp the menu's row order and labels, host-tested
    └── RefMenu.h/.cpp      the settings menu
```

In the testing section, replace:

```markdown
`RefZone.cpp`, `RefSegments.cpp` and `RefSport.cpp` are pure arithmetic and
compile on a host, so the daylight saving rule, the digit layout and the
preset table are all checked off the watch — this is exactly the kind of bug
(a changeover date, a digit running off the panel, a clamp) that is expensive
to find on hardware and cheap to catch here.
```

with:

```markdown
`RefZone.cpp`, `RefSegments.cpp`, `RefSport.cpp`, `RefWifi.cpp` and
`RefMenuItems.cpp` carry no panel and no GPIO, so they compile on a host and
the daylight saving rule, the digit layout, the preset table, the WiFi flag and
the menu's row order are all checked off the watch — this is exactly the kind
of bug (a changeover date, a digit running off the panel, a clamp, a menu row
in the wrong place) that is expensive to find on hardware and cheap to catch
here.
```

and add, immediately before the closing "Nothing else here has automated
tests" line:

```markdown
`tests/wifi_test.cpp` checks that a watch with nothing stored — or with NVS
unavailable — reads as never set up, and that the flag survives a simulated
reboot in both directions.

`tests/menu_items_test.cpp` pins the menu's row order with and without WiFi
set up, that **Sync NTP** is absent in the first case and second in the
second, and that **Setup WiFi** shifts from slot 4 to slot 5 when it appears —
which is the case the menu's redraw depends on, since running **Setup WiFi**
inserts a row above the one the user is standing on. It also checks that every
label fits the 18 glyphs the panel has room for, and that the removed buzz
test has not crept back in.
```

- [ ] **Step 13: Commit**

```bash
git add RefCounter/RefMenu.cpp RefCounter/RefCounter.ino README.md
git commit -m "feat: reorder the settings menu, gate Sync NTP on WiFi, drop the buzz test"
```

---

## Done when

- `./tests/run.sh` prints six `PASSED` blocks.
- `pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3` prints four `SUCCESS` lines.
- `grep -rn "ITEM_BUZZ\|showBuzz\|Vibrate Motor" RefCounter/ tests/` prints nothing.
- The README's menu table matches the shipped order and carries the upgrade note about re-running **Setup WiFi**.
