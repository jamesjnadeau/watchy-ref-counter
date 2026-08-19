# Sport Timing Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user pick a sport's timing preset from the on-watch menu — Football, Lacrosse, NCAA/NFHS baseball and softball — plus an editable Custom slot, with countdowns up to 199 seconds.

**Architecture:** A new `RefSport` module owns the preset table and the selected index, persisted in NVS exactly the way `RefZone` already persists the time zone. The two hard-coded countdown constants leave `settings.h` and everything that read them — the main loop, the ready screen, the buzz marks — reads the active preset instead. The two-digit seven-segment display gains a skinny leading `1`, with the placement arithmetic split into a pure `RefSegments` header so it can be tested on a host alongside `RefZone`.

**Tech Stack:** C++11 (Arduino ESP32 core), GxEPD2, Adafruit GFX, ESP32 `Preferences` (NVS). Host tests are plain g++ against `tests/stub/Preferences.h`.

**Spec:** [`docs/superpowers/specs/2026-08-18-sport-presets.md`](../specs/2026-08-18-sport-presets.md)

## Global Constraints

- **No new libraries.** GxEPD2, Adafruit GFX and WiFiManager stay the only three.
- **Maximum countdown value is 199 seconds**, minimum 1. Warning marks and the final-countdown value may be 0, meaning off.
- **Preset names are 9 characters or fewer**; descriptions 14 or fewer. Names are what make the menu rows fit `FreeMonoBold9pt7b` (11px per glyph) on the 200px panel; the description cap is set by `pickSport` drawing the description and the position counter on the same baseline -- 14 glyphs is the last width that stays clear of the counter.
- **Warning marks are in seconds remaining**, not elapsed. ReadyRef quotes elapsed; the conversion is done once, in the spec's preset table.
- **New persistence follows the `RefZone` pattern**: a `Preferences` handle opened and closed per read/write, defaults from `settings.h` when nothing is stored, values clamped on the way in and out.
- **`RefSegments.cpp` and `RefSport.cpp` must compile on a host** with no Arduino headers beyond the `Preferences` stub. Neither may include `RefPanel.h`, `board.h` or anything from GxEPD2.
- **Every task ends with all four PlatformIO envs building**: `watchy_v2`, `watchy_v15`, `watchy_v10`, `watchy_v3`.
- **Nothing here can be verified on hardware.** The repo has never run on a watch (README *Known limitations*). "Verify" means the host tests pass and all four envs compile; do not claim otherwise in commit messages.

---

### Task 1: Countdown layout arithmetic

Pull the seven-segment geometry out of `RefDisplay.cpp` into a pure header plus source file, and add the placement rule for a leading `1`. This is the only genuinely testable piece of the display work, so it goes first and on its own.

**Files:**
- Create: `RefCounter/RefSegments.h`
- Create: `RefCounter/RefSegments.cpp`
- Create: `tests/segments_test.cpp`
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct SegStyle { int16_t w, h, t, gap; }`, `struct CountLayout { bool hundreds; int16_t oneX, tensX, onesX, width; uint8_t tens, ones; }`, and `CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW)`. Task 2 draws from these; nothing else uses them.

- [ ] **Step 1: Write the failing test**

Create `tests/segments_test.cpp`:

```cpp
// Host test for the countdown digit placement in RefSegments.cpp. Pure
// arithmetic: no Arduino headers, no panel, so it runs anywhere.
#include "RefSegments.h"
#include <cstdio>

static int failures = 0;

// The two styles RefDisplay actually uses, repeated here so the test pins the
// numbers the panel is laid out around.
static const SegStyle BIG   = {70, 116, 15, 16};
static const SegStyle SMALL = {32, 52, 8, 8};
static const int16_t  SCREEN_W = 200;

static void expectEq(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-44s want %d got %d\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-44s %d\n", what, got);
  }
}

static void expectFits(const char *what, const CountLayout &l, const SegStyle &s) {
  const int16_t left  = l.hundreds ? l.oneX : l.tensX;
  const int16_t right = l.onesX + s.w;
  if (left < 0 || right > SCREEN_W) {
    printf("FAIL %-44s spans %d..%d\n", what, left, right);
    failures++;
  } else {
    printf("ok   %-44s spans %d..%d\n", what, left, right);
  }
}

int main() {
  // Two digits: unchanged from what the panel draws today.
  CountLayout l = layoutCount(40, BIG, SCREEN_W);
  expectEq("40 big: no hundreds bar", l.hundreds, 0);
  expectEq("40 big: tens digit", l.tens, 4);
  expectEq("40 big: ones digit", l.ones, 0);
  expectEq("40 big: width", l.width, 156);
  expectEq("40 big: tens x", l.tensX, 22);
  expectEq("40 big: ones x", l.onesX, 108);

  // Three digits: skinny 1 at the left, pair shifted right.
  l = layoutCount(120, BIG, SCREEN_W);
  expectEq("120 big: hundreds bar", l.hundreds, 1);
  expectEq("120 big: tens digit", l.tens, 2);
  expectEq("120 big: ones digit", l.ones, 0);
  expectEq("120 big: width", l.width, 187);
  expectEq("120 big: one x", l.oneX, 6);
  expectEq("120 big: tens x", l.tensX, 37);
  expectEq("120 big: ones x", l.onesX, 123);

  // Boundaries either side of the layout change.
  expectEq("100 big: hundreds bar", layoutCount(100, BIG, SCREEN_W).hundreds, 1);
  expectEq("99 big: no hundreds bar", layoutCount(99, BIG, SCREEN_W).hundreds, 0);

  // Above the ceiling, clamp rather than draw a fourth digit.
  l = layoutCount(200, BIG, SCREEN_W);
  expectEq("200 big: clamps to 199 tens", l.tens, 9);
  expectEq("200 big: clamps to 199 ones", l.ones, 9);
  expectEq("200 big: clamps to 199 hundreds", l.hundreds, 1);

  // Zero still draws two digits.
  l = layoutCount(0, BIG, SCREEN_W);
  expectEq("0 big: tens digit", l.tens, 0);
  expectEq("0 big: ones digit", l.ones, 0);
  expectEq("0 big: no hundreds bar", l.hundreds, 0);

  // The small style is what the ready screen stacks; 120 has to fit there too.
  l = layoutCount(120, SMALL, SCREEN_W);
  expectEq("120 small: width", l.width, 88);
  expectEq("120 small: one x", l.oneX, 56);

  // Nothing may run off either edge of the panel, at any value.
  for (uint16_t v = 0; v <= 199; v++) {
    char what[48];
    snprintf(what, sizeof(what), "%u big on panel", (unsigned)v);
    expectFits(what, layoutCount(v, BIG, SCREEN_W), BIG);
  }

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run:

```bash
g++ -std=c++17 -Wall -Wextra -IRefCounter -o /tmp/segments_test tests/segments_test.cpp RefCounter/RefSegments.cpp
```

Expected: FAIL — `RefSegments.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `RefCounter/RefSegments.h`:

```cpp
#ifndef REF_SEGMENTS_H
#define REF_SEGMENTS_H

#include <stdint.h>

// Where the countdown's seven-segment glyphs land on the panel.
//
// This is pure arithmetic with no Arduino or GxEPD2 dependency, so the layout
// can be checked on a host. RefDisplay owns the actual drawing; this file only
// decides where each glyph goes.
//
// Values of 100 and up get a skinny leading "1" -- just the two vertical bars,
// one segment thickness wide -- to the left of the two full-size digits, which
// shift right to make room. 199 is the ceiling; there is no room for a fourth
// glyph and no sport needs one.

// Geometry of one seven-segment digit and the space that follows it.
struct SegStyle {
  int16_t w;   // width of one digit
  int16_t h;   // height of one digit
  int16_t t;   // segment thickness
  int16_t gap; // space between adjacent glyphs
};

struct CountLayout {
  bool    hundreds; // draw the skinny leading 1
  int16_t oneX;     // left edge of that bar; ignore when !hundreds
  int16_t tensX;    // left edge of the tens digit
  int16_t onesX;    // left edge of the ones digit
  int16_t width;    // total ink width, so a caller can size a refresh window
  uint8_t tens;
  uint8_t ones;
};

// Largest value the layout can draw. Anything above is clamped to it.
static const uint16_t SEG_MAX_VALUE = 199;

// Lay `value` out horizontally centred on a `screenW` wide panel.
CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW);

#endif // REF_SEGMENTS_H
```

- [ ] **Step 4: Write the implementation**

Create `RefCounter/RefSegments.cpp`:

```cpp
#include "RefSegments.h"

CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW) {
  if (value > SEG_MAX_VALUE) {
    value = SEG_MAX_VALUE;
  }

  CountLayout l = {};
  l.hundreds = value >= 100;
  l.tens     = (uint8_t)((value / 10) % 10);
  l.ones     = (uint8_t)(value % 10);

  const int16_t pairW = (int16_t)(s.w * 2 + s.gap);
  l.width = l.hundreds ? (int16_t)(s.t + s.gap + pairW) : pairW;

  const int16_t x = (int16_t)((screenW - l.width) / 2);
  l.oneX  = x;
  l.tensX = l.hundreds ? (int16_t)(x + s.t + s.gap) : x;
  l.onesX = (int16_t)(l.tensX + s.w + s.gap);
  return l;
}
```

- [ ] **Step 5: Run the test and make sure it passes**

Run:

```bash
g++ -std=c++17 -Wall -Wextra -IRefCounter -o /tmp/segments_test tests/segments_test.cpp RefCounter/RefSegments.cpp && /tmp/segments_test
```

Expected: PASS — every line `ok`, last line `PASSED (0 failures)`.

- [ ] **Step 6: Wire it into the test runner**

`tests/run.sh` currently compiles both tests against `RefZone.cpp`. Different tests now need different sources, so replace the loop. The whole file becomes:

```bash
#!/usr/bin/env bash
# Host tests for the parts of the sketch that are pure arithmetic.
#
# RefZone.cpp, RefSegments.cpp and RefSport.cpp are the only files with logic
# worth testing off the watch: no panel, no GPIO, and the sort of bugs -- a
# daylight saving date, a digit running off the edge of the screen, a clamp --
# that are expensive to find on hardware. They compile here against a stub
# Preferences; everything else is the shipped source.
#
#   ./tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT

declare -A SOURCES=(
  [tz_test]="RefCounter/RefZone.cpp"
  [tz_edges]="RefCounter/RefZone.cpp"
  [segments_test]="RefCounter/RefSegments.cpp"
)

for t in tz_test tz_edges segments_test; do
  $CXX -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter \
       -o "$out/$t" "tests/$t.cpp" ${SOURCES[$t]}
  echo "=== $t"
  "$out/$t"
  echo
done
```

- [ ] **Step 7: Run the whole suite**

Run:

```bash
./tests/run.sh
```

Expected: all three tests print `PASSED (0 failures)`, and the script exits 0.

- [ ] **Step 8: Commit**

```bash
git add RefCounter/RefSegments.h RefCounter/RefSegments.cpp tests/segments_test.cpp tests/run.sh
git commit -m "feat: countdown digit layout with a leading 1 for values over 99"
```

---

### Task 2: Draw the leading 1

Make `RefDisplay` use `layoutCount`, and widen the per-tick refresh window so a hundreds bar can never be left standing on the panel. No behaviour changes yet — nothing feeds it a value over 99 until Task 4 — but the drawing path is complete after this.

**Files:**
- Modify: `RefCounter/RefDisplay.cpp` (the `SegStyle` definition, `drawPair`/`drawValue`, `DIGITS_WIN_*`, `drawBody`, `renderDigits`)

**Interfaces:**
- Consumes: `SegStyle`, `CountLayout`, `layoutCount` from Task 1.
- Produces: nothing new in `RefDisplay.h`. Internally, `drawCount(uint16_t value, int16_t y, const SegStyle &s, uint16_t colour)` replaces `drawValue`. `drawPair` stays, because `drawDigitPair` (the menu's Set Time screen) still needs a plain two-digit pair.

- [ ] **Step 1: Include the new header and drop the local SegStyle**

In `RefCounter/RefDisplay.cpp`, add the include next to the others at the top:

```cpp
#include "RefDisplay.h"

#include "Buttons.h"
#include "RefPanel.h"
#include "RefSegments.h"
#include "board.h"
#include "settings.h"
```

Then delete the local `struct SegStyle { ... };` definition (it sits just under the seven-segment ASCII diagram, immediately above `const SegStyle STYLE_BIG`). Leave the diagram comment and the `STYLE_BIG` / `STYLE_SMALL` constants exactly as they are — they are now instances of the header's type.

- [ ] **Step 2: Widen the per-tick refresh window**

The tick window is hard-coded around a two-digit box. A 199 spans x 6..193, so it has to cover the full width or the hundreds bar survives a redraw. Replace:

```cpp
const int16_t DIGITS_WIN_X = 16;
const int16_t DIGITS_WIN_Y = 40;
const int16_t DIGITS_WIN_W = 176;
const int16_t DIGITS_WIN_H = 124;
```

with:

```cpp
// The window refreshed on each tick. It has to cover the widest the countdown
// can get, which is 199 in STYLE_BIG: a skinny hundreds bar at x 6 out to the
// right edge of the ones digit at x 193. GxEPD2 rounds x and w out to a
// multiple of 8 regardless, so this is the full panel width rather than a
// nearly-full one pretending to be tighter.
const int16_t DIGITS_WIN_X = 0;
const int16_t DIGITS_WIN_Y = 40;
const int16_t DIGITS_WIN_W = 200;
const int16_t DIGITS_WIN_H = 124;
```

- [ ] **Step 3: Add the hundreds bar and the count drawing**

`drawPair` stays as it is. Directly below it, replace the existing `drawValue`:

```cpp
// A zero-padded two digit value, centred horizontally.
void drawValue(uint16_t value, int16_t y, const SegStyle &s) {
  drawPair((SCREEN_W - (s.w * 2 + s.gap)) / 2, y, value, s, FG);
}
```

with the hundreds bar plus the count drawing:

```cpp
// The skinny leading "1": segments b and c only, at the same heights drawDigit
// puts them, but one thickness wide instead of a whole digit.
void drawOneBar(int16_t x, int16_t y, const SegStyle &s, uint16_t colour) {
  const int16_t t    = s.t;
  const int16_t midY = (s.h - t) / 2;
  const int16_t upH  = midY - t;
  const int16_t lowY = midY + t;
  const int16_t lowH = s.h - t - lowY;

  display.fillRect(x, y + t, t, upH, colour);
  display.fillRect(x, y + lowY, t, lowH, colour);
}

// A countdown value, 0..199, centred horizontally. Where each glyph goes is
// RefSegments' decision, so it can be checked on a host.
void drawCount(uint16_t value, int16_t y, const SegStyle &s, uint16_t colour) {
  const CountLayout l = layoutCount(value, s, SCREEN_W);
  if (l.hundreds) {
    drawOneBar(l.oneX, y, s, colour);
  }
  drawDigit(l.tensX, y, l.tens, s, colour);
  drawDigit(l.onesX, y, l.ones, s, colour);
}
```

- [ ] **Step 4: Point the two callers at it**

In `drawBody`, replace the three `drawValue(...)` calls:

```cpp
void drawBody(const View &v) {
  if (v.state == STATE_IDLE) {
    // Stack both clocks so they line up with the two right-hand buttons.
    drawCount(TIMER_LONG_SECONDS, ROW1_Y, STYLE_SMALL, FG);
    drawRowMarker(ROW1_Y, STYLE_SMALL);
    drawCount(TIMER_SHORT_SECONDS, ROW2_Y, STYLE_SMALL, FG);
    drawRowMarker(ROW2_Y, STYLE_SMALL);
    return;
  }
  drawCount(v.secondsLeft, BIG_Y, STYLE_BIG, FG);
}
```

and in `renderDigits`:

```cpp
void renderDigits(const View &v) {
  display.setTextColor(FG);
  display.fillRect(DIGITS_WIN_X, DIGITS_WIN_Y, DIGITS_WIN_W, DIGITS_WIN_H, BG);
  drawCount(v.secondsLeft, BIG_Y, STYLE_BIG, FG);
  display.displayWindow(DIGITS_WIN_X, DIGITS_WIN_Y, DIGITS_WIN_W, DIGITS_WIN_H);
}
```

- [ ] **Step 5: Build all four envs**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines, no warnings from `RefDisplay.cpp` or `RefSegments.cpp`. If `drawValue` is reported as defined-but-unused, it was not fully removed — delete it.

- [ ] **Step 6: Commit**

```bash
git add RefCounter/RefDisplay.cpp
git commit -m "feat: draw countdowns up to 199 with a leading hundreds bar"
```

---

### Task 3: The RefSport module

The preset table, the selected index, and the Custom slot — all persisted in NVS the way `RefZone` persists the zone. Nothing consumes it yet; Task 4 wires it in.

**Files:**
- Create: `RefCounter/RefSport.h`
- Create: `RefCounter/RefSport.cpp`
- Create: `tests/sport_test.cpp`
- Modify: `tests/stub/Preferences.h`
- Modify: `RefCounter/settings.h` (add the new constants only; Task 4 removes the old ones)
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: `DEFAULT_SPORT`, `CUSTOM_LONG_SECONDS`, `CUSTOM_SHORT_SECONDS`, `CUSTOM_WARN_SECONDS`, `CUSTOM_WARN2_SECONDS`, `CUSTOM_FINAL_FROM` from `settings.h` (added in Step 1 below).
- Produces: `RefSport::Preset` (fields `name`, `description`, `longSeconds`, `shortSeconds`, `warnAtSeconds`, `warn2AtSeconds`, `finalCountdownFrom`), and the functions `count()`, `preset(uint8_t)`, `isCustom(uint8_t)`, `begin()`, `index()`, `setIndex(uint8_t)`, `active()`, `custom()`, `setCustom(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t)`, plus the constants `MIN_CLOCK_SECONDS` and `MAX_SECONDS`. Tasks 4, 6 and 7 all call these.

- [ ] **Step 1: Add the new settings constants**

In `RefCounter/settings.h`, immediately after the `TIMER_SHORT_SECONDS` line (the old constants stay for now — Task 4 removes them, and removing them here would break the build mid-task), insert:

```cpp
// --- Sport preset ----------------------------------------------------------
// Which preset the watch starts on. This is only the starting point: the
// menu's "Sport" entry picks one on the watch and stores it, and from then on
// the stored value wins. Must match one of the names in RefSport.cpp, which
// are:
//
//   Football   Lacrosse   Base NCAA  Base NFHS  Soft NCAA  Soft NFHS  Custom
//
static const char DEFAULT_SPORT[] = "Football";

// Factory values for the menu's "Custom" slot, likewise overridden once "Edit
// Custom" has been used. Clocks are 1..199 seconds; the three marks are
// 0..199, where 0 means off. All three marks are in seconds *remaining*.
static const uint16_t CUSTOM_LONG_SECONDS  = 40;
static const uint16_t CUSTOM_SHORT_SECONDS = 25;
static const uint16_t CUSTOM_WARN_SECONDS  = 10;
static const uint16_t CUSTOM_WARN2_SECONDS = 0;
static const uint16_t CUSTOM_FINAL_FROM    = 5;
```

- [ ] **Step 2: Teach the Preferences stub to actually store things**

The existing stub always fails `begin()`, which is what the time zone tests want — the `settings.h` defaults then apply. The sport test needs both behaviours, so the stub gets a switch that defaults to off. Replace the whole of `tests/stub/Preferences.h`:

```cpp
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
```

- [ ] **Step 3: Write the failing test**

Create `tests/sport_test.cpp`:

```cpp
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

  PreferencesStub::enable(false);
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
```

- [ ] **Step 4: Run it to make sure it fails**

Run:

```bash
g++ -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter -o /tmp/sport_test tests/sport_test.cpp RefCounter/RefSport.cpp
```

Expected: FAIL — `RefSport.h: No such file or directory`.

- [ ] **Step 5: Write the header**

Create `RefCounter/RefSport.h`:

```cpp
#ifndef REF_SPORT_H
#define REF_SPORT_H

#include <stdint.h>

// Sport timing presets: the two countdowns the right-hand buttons start, and
// the marks the buzzer fires on along the way.
//
// The shape here follows RefZone: a fixed table in flash, one selected index
// held in NVS, and a settings.h default used until the menu has been touched.
// The last entry, "Custom", is the only editable one; its numbers live in NVS
// too, so a reflash does not wipe them.
//
// Every mark is in seconds *remaining*, matching what the sketch counts down.
// ReadyRef quote their pre-warnings in seconds elapsed; on a 40 second clock
// their "30 second pre-warning" is 10 remaining, which is what Football ships.
namespace RefSport {

// The display draws at most a skinny leading 1 plus two full digits, so no
// value may exceed 199. A clock of 0 would expire the instant it started.
static const uint16_t MIN_CLOCK_SECONDS = 1;
static const uint16_t MAX_SECONDS       = 199;

struct Preset {
  const char *name;        // menu label, <= 9 chars so the rows fit
  const char *description; // <= 14 chars, shown under the picker
  uint16_t longSeconds;    // top-right button
  uint16_t shortSeconds;   // bottom-right button
  uint16_t warnAtSeconds;  // first early warning; 0 = off
  uint16_t warn2AtSeconds; // second early warning; 0 = off
  uint16_t finalCountdownFrom; // buzz each of the last N seconds; 0 = off
};

// Number of presets in the table, Custom included.
uint8_t count();

// The preset at `index`, or the first one if `index` is out of range.
Preset preset(uint8_t index);

// True for the single editable entry.
bool isCustom(uint8_t index);

// Load the saved sport and the Custom slot out of NVS, falling back to the
// defaults in settings.h. Call once at startup.
void begin();

uint8_t index();
void setIndex(uint8_t index); // persists immediately

// The selected preset. This is what the sketch reads every time it starts a
// clock or decides whether to buzz.
Preset active();

// The Custom slot as stored.
Preset custom();

// Overwrite the Custom slot and persist it. Every field is clamped first, so a
// caller cannot store a value the display could not draw: the two clocks to
// 1..MAX_SECONDS, the three marks to 0..MAX_SECONDS.
void setCustom(uint16_t longSeconds, uint16_t shortSeconds,
               uint16_t warnAtSeconds, uint16_t warn2AtSeconds,
               uint16_t finalCountdownFrom);

} // namespace RefSport

#endif // REF_SPORT_H
```

- [ ] **Step 6: Write the implementation**

Create `RefCounter/RefSport.cpp`:

```cpp
#include "RefSport.h"

#include <Preferences.h>
#include <string.h>

#include "settings.h"

namespace RefSport {
namespace {

// The fixed presets, in the order the picker lists them. Values follow the
// ReadyRef model line-up; see docs/superpowers/specs/2026-08-18-sport-presets.md
// for which model each row corresponds to.
//
// Football reproduces what this firmware shipped with, so a watch that has
// never opened the menu behaves exactly as it did before presets existed.
const Preset FIXED[] = {
    {"Football",  "football",       40, 25, 10,  0, 5},
    {"Lacrosse",  "lacrosse shot", 120, 20, 30, 10, 5},
    {"Base NCAA", "NCAA baseball", 120, 20, 30, 10, 5},
    {"Base NFHS", "NFHS baseball",  80, 20, 30, 10, 5},
    {"Soft NCAA", "NCAA softball",  90, 20, 30, 10, 5},
    {"Soft NFHS", "NFHS softball",  60, 20, 20, 10, 5},
};
const uint8_t FIXED_COUNT   = sizeof(FIXED) / sizeof(FIXED[0]);
const uint8_t CUSTOM_INDEX  = FIXED_COUNT;
const uint8_t PRESET_COUNT  = FIXED_COUNT + 1;

// NVS. The sport is a property of what the official is working today, not of
// the firmware, so it survives a reflash the way the time zone does.
const char *NVS_NAMESPACE = "refsport";
const char *NVS_KEY_SPORT = "sport";
const char *NVS_KEY_LONG  = "clong";
const char *NVS_KEY_SHORT = "cshort";
const char *NVS_KEY_WARN  = "cwarn";
const char *NVS_KEY_WARN2 = "cwarn2";
const char *NVS_KEY_FINAL = "cfinal";

uint8_t selected = 0;

// The Custom slot. Its two strings never change; the numbers are replaced from
// settings.h in begin() and from NVS or the editor after that.
Preset customPreset = {"Custom", "user set", 40, 25, 10, 0, 5};

uint8_t clampIndex(uint8_t i) { return i < PRESET_COUNT ? i : 0; }

uint16_t clampClock(uint16_t v) {
  if (v < MIN_CLOCK_SECONDS) {
    return MIN_CLOCK_SECONDS;
  }
  return v > MAX_SECONDS ? MAX_SECONDS : v;
}

// Marks may be 0, which turns them off.
uint16_t clampMark(uint16_t v) { return v > MAX_SECONDS ? MAX_SECONDS : v; }

} // namespace

uint8_t count() { return PRESET_COUNT; }

bool isCustom(uint8_t i) { return clampIndex(i) == CUSTOM_INDEX; }

Preset preset(uint8_t i) {
  i = clampIndex(i);
  return i == CUSTOM_INDEX ? customPreset : FIXED[i];
}

void begin() {
  // Resolve the compiled-in default first, so a watch with nothing stored yet
  // still comes up on a sensible pair of clocks.
  selected = 0;
  if (strcmp(customPreset.name, DEFAULT_SPORT) == 0) {
    selected = CUSTOM_INDEX;
  } else {
    for (uint8_t i = 0; i < FIXED_COUNT; i++) {
      if (strcmp(FIXED[i].name, DEFAULT_SPORT) == 0) {
        selected = i;
        break;
      }
    }
  }

  customPreset.longSeconds        = clampClock(CUSTOM_LONG_SECONDS);
  customPreset.shortSeconds       = clampClock(CUSTOM_SHORT_SECONDS);
  customPreset.warnAtSeconds      = clampMark(CUSTOM_WARN_SECONDS);
  customPreset.warn2AtSeconds     = clampMark(CUSTOM_WARN2_SECONDS);
  customPreset.finalCountdownFrom = clampMark(CUSTOM_FINAL_FROM);

  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) { // read only
    selected = clampIndex(prefs.getUChar(NVS_KEY_SPORT, selected));
    customPreset.longSeconds =
        clampClock(prefs.getUShort(NVS_KEY_LONG, customPreset.longSeconds));
    customPreset.shortSeconds =
        clampClock(prefs.getUShort(NVS_KEY_SHORT, customPreset.shortSeconds));
    customPreset.warnAtSeconds =
        clampMark(prefs.getUShort(NVS_KEY_WARN, customPreset.warnAtSeconds));
    customPreset.warn2AtSeconds =
        clampMark(prefs.getUShort(NVS_KEY_WARN2, customPreset.warn2AtSeconds));
    customPreset.finalCountdownFrom =
        clampMark(prefs.getUShort(NVS_KEY_FINAL, customPreset.finalCountdownFrom));
    prefs.end();
  }
}

uint8_t index() { return selected; }

void setIndex(uint8_t i) {
  selected = clampIndex(i);
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUChar(NVS_KEY_SPORT, selected);
    prefs.end();
  }
}

Preset active() { return preset(selected); }

Preset custom() { return customPreset; }

void setCustom(uint16_t longSeconds, uint16_t shortSeconds,
               uint16_t warnAtSeconds, uint16_t warn2AtSeconds,
               uint16_t finalCountdownFrom) {
  customPreset.longSeconds        = clampClock(longSeconds);
  customPreset.shortSeconds       = clampClock(shortSeconds);
  customPreset.warnAtSeconds      = clampMark(warnAtSeconds);
  customPreset.warn2AtSeconds     = clampMark(warn2AtSeconds);
  customPreset.finalCountdownFrom = clampMark(finalCountdownFrom);

  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUShort(NVS_KEY_LONG, customPreset.longSeconds);
    prefs.putUShort(NVS_KEY_SHORT, customPreset.shortSeconds);
    prefs.putUShort(NVS_KEY_WARN, customPreset.warnAtSeconds);
    prefs.putUShort(NVS_KEY_WARN2, customPreset.warn2AtSeconds);
    prefs.putUShort(NVS_KEY_FINAL, customPreset.finalCountdownFrom);
    prefs.end();
  }
}

} // namespace RefSport
```

- [ ] **Step 7: Run the test and make sure it passes**

Run:

```bash
g++ -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter -o /tmp/sport_test tests/sport_test.cpp RefCounter/RefSport.cpp && /tmp/sport_test
```

Expected: PASS — `PASSED (0 failures)`.

- [ ] **Step 8: Add it to the runner and confirm the zone tests still pass**

In `tests/run.sh`, add the source mapping and the test name:

```bash
declare -A SOURCES=(
  [tz_test]="RefCounter/RefZone.cpp"
  [tz_edges]="RefCounter/RefZone.cpp"
  [segments_test]="RefCounter/RefSegments.cpp"
  [sport_test]="RefCounter/RefSport.cpp"
)

for t in tz_test tz_edges segments_test sport_test; do
```

Run:

```bash
./tests/run.sh
```

Expected: all four tests pass. `tz_test` matters most here — the rewritten stub must not have changed its behaviour, so it should still report the same `PASSED (0 failures)` it did before.

- [ ] **Step 9: Commit**

```bash
git add RefCounter/RefSport.h RefCounter/RefSport.cpp RefCounter/settings.h tests/sport_test.cpp tests/stub/Preferences.h tests/run.sh
git commit -m "feat: sport preset table with an editable custom slot"
```

---

### Task 4: Run the clock off the active preset

Retire the four compile-time timing constants and read `RefSport::active()` instead — in the main loop, in the buzz marks, and on the ready screen. After this the firmware behaves identically for football and correctly for every other preset, even though nothing can select one yet.

**Files:**
- Modify: `RefCounter/settings.h` (remove four constants, add one)
- Modify: `RefCounter/RefCounter.ino` (`buzzForMark`, `startTimer` callers, `setup`)
- Modify: `RefCounter/RefDisplay.cpp` (`drawBody`, `drawFooter`)

**Interfaces:**
- Consumes: `RefSport::begin()`, `RefSport::active()` and the `Preset` fields from Task 3; `drawCount` from Task 2.
- Produces: nothing new. `WARNING_BUZZ_COUNT_2` is added to `settings.h` and used by `buzzForMark`.

- [ ] **Step 1: Retire the old constants**

In `RefCounter/settings.h`, delete these four declarations and the comment blocks that introduce them — `TIMER_LONG_SECONDS`, `TIMER_SHORT_SECONDS` (the whole `--- Countdown lengths ---` section), `WARNING_AT_SECONDS`, and `FINAL_COUNTDOWN_FROM`. They now live in the preset table.

Keep `WARNING_BUZZ_COUNT` and add a second count beside it, so the two marks are audibly distinguishable. The buzzer block becomes:

```cpp
// Early warning: when a preset's first mark is reached, buzz this many times;
// its second mark buzzes WARNING_BUZZ_COUNT_2 times. Either may be 0 to
// silence that mark for every preset at once. Which second each mark falls on
// is per-sport and lives in RefSport.cpp; the menu's "Edit Custom" screen sets
// them for the Custom preset.
//
// Note: a mark at or below a preset's final-countdown value is swallowed by
// the per-second countdown, which buzzes there anyway.
static const uint8_t WARNING_BUZZ_COUNT   = 1;
static const uint8_t WARNING_BUZZ_COUNT_2 = 2;
```

Also bump the version, since the About screen is the only place a user can tell which firmware is on the watch:

```cpp
static const char REF_COUNTER_VERSION[] = "1.1";
```

- [ ] **Step 2: Read the preset in the sketch**

In `RefCounter/RefCounter.ino`, add the include alongside the others:

```cpp
#include "RefMenu.h"
#include "RefClock.h"
#include "RefSport.h"
#include "board.h"
```

Change the `durationSec` initialiser — it can no longer name a constant, and the real value is set the moment a clock starts:

```cpp
static uint16_t durationSec = 0;
```

Replace `buzzForMark` entirely:

```cpp
// Buzz for whatever mark the clock just landed on, using the active preset's
// numbers. Ordering matters: the per-second countdown takes precedence over
// both warnings, so a mark set inside finalCountdownFrom is silently swallowed.
static void buzzForMark(uint16_t secondsLeft) {
  const RefSport::Preset p = RefSport::active();

  if (secondsLeft == 0) {
    Buzzer::pulse(BUZZ_EXPIRE_MS);
  } else if (p.finalCountdownFrom > 0 && secondsLeft <= p.finalCountdownFrom) {
    Buzzer::pulse(BUZZ_SHORT_MS);
  } else if (WARNING_BUZZ_COUNT > 0 && p.warnAtSeconds > 0 &&
             secondsLeft == p.warnAtSeconds) {
    Buzzer::pulse(WARNING_BUZZ_COUNT, BUZZ_SHORT_MS, BUZZ_GAP_MS);
  } else if (WARNING_BUZZ_COUNT_2 > 0 && p.warn2AtSeconds > 0 &&
             secondsLeft == p.warn2AtSeconds) {
    Buzzer::pulse(WARNING_BUZZ_COUNT_2, BUZZ_SHORT_MS, BUZZ_GAP_MS);
  }
}
```

In `loop()`, the two timer buttons take their duration from the preset:

```cpp
  if (Buttons::heldFor(Buttons::LONG_TIMER, TIMER_HOLD_MS)) {
    startTimer(RefSport::active().longSeconds);
    return;
  }
  if (Buttons::heldFor(Buttons::SHORT_TIMER, TIMER_HOLD_MS)) {
    startTimer(RefSport::active().shortSeconds);
    return;
  }
```

And in `setup()`, load the stored preset before anything draws the ready screen:

```cpp
  Buzzer::begin();
  Buttons::begin();
  RefSport::begin();
  RefDisplay::begin();
```

- [ ] **Step 3: Read the preset on the ready screen**

In `RefCounter/RefDisplay.cpp`, add the include:

```cpp
#include "RefPanel.h"
#include "RefSegments.h"
#include "RefSport.h"
#include "board.h"
```

`drawBody` stacks the active preset's two clocks rather than the constants:

```cpp
void drawBody(const View &v) {
  if (v.state == STATE_IDLE) {
    // Stack both clocks so they line up with the two right-hand buttons.
    const RefSport::Preset p = RefSport::active();
    drawCount(p.longSeconds, ROW1_Y, STYLE_SMALL, FG);
    drawRowMarker(ROW1_Y, STYLE_SMALL);
    drawCount(p.shortSeconds, ROW2_Y, STYLE_SMALL, FG);
    drawRowMarker(ROW2_Y, STYLE_SMALL);
    return;
  }
  drawCount(v.secondsLeft, BIG_Y, STYLE_BIG, FG);
}
```

- [ ] **Step 4: Name the sport on the ready screen**

Which preset is loaded is the one thing worth confirming before kickoff, and there is no room on the footer for both that and `HOLD TO START`. Add `#include <ctype.h>` at the top of `RefCounter/RefDisplay.cpp`, then replace `drawFooter`:

```cpp
void drawFooter(const View &v) {
  // Room for the longest preset name plus its terminator.
  static char sportUpper[12];
  const char *hint;

  switch (v.state) {
  case STATE_RUNNING:
    hint = "BTM LEFT = CLEAR";
    break;
  case STATE_EXPIRED:
    hint = "TIME EXPIRED";
    break;
  default: {
    // On the ready screen the footer says which preset is loaded. That is the
    // thing worth checking before a game; "hold to start" is in the manual.
    const char *name = RefSport::active().name;
    size_t i = 0;
    for (; name[i] != '\0' && i < sizeof(sportUpper) - 1; i++) {
      sportUpper[i] = (char)toupper((unsigned char)name[i]);
    }
    sportUpper[i] = '\0';
    hint = sportUpper;
    break;
  }
  }

  display.drawFastHLine(0, FOOTER_RULE_Y, SCREEN_W, FG);
  display.setFont(&FreeMonoBold9pt7b);
  printCentred(hint, FOOTER_BASELINE);
}
```

- [ ] **Step 5: Build all four envs**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines. Any `TIMER_LONG_SECONDS was not declared` error points at a caller that was missed — grep for the four removed names and fix it:

```bash
grep -rn "TIMER_LONG_SECONDS\|TIMER_SHORT_SECONDS\|WARNING_AT_SECONDS\|FINAL_COUNTDOWN_FROM" RefCounter/ tests/
```

Expected: no output.

- [ ] **Step 6: Confirm the host tests still pass**

Run:

```bash
./tests/run.sh
```

Expected: four tests, all `PASSED (0 failures)`.

- [ ] **Step 7: Commit**

```bash
git add RefCounter/settings.h RefCounter/RefCounter.ino RefCounter/RefDisplay.cpp
git commit -m "feat: run the play clock off the selected sport preset"
```

---

### Task 5: Scroll the settings menu

The main menu grows from 7 entries to 9 in the next two tasks, and 9 rows at `MENU_ROW_H` 25 run 25 pixels off the bottom of the panel. Give `drawMenu` the same seven-row scrolling window the time zone picker already uses. Doing it before the new entries exist keeps this reviewable on its own.

**Files:**
- Modify: `RefCounter/RefMenu.cpp` (`drawMenu`, and a reset in `open`)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing outside the file. `drawMenu(uint8_t index, bool partial)` keeps its signature.

- [ ] **Step 1: Add the scroll window to drawMenu**

In `RefCounter/RefMenu.cpp`, just under `const int16_t MENU_ROW_H = 25;`, add:

```cpp
// Rows that fit on the panel at MENU_ROW_H. The list is longer than this, so
// drawMenu scrolls a window over it, the way pickTimeZone does over the zones.
const uint8_t MENU_VISIBLE = 7;

// Top of the visible window. Lives outside drawMenu so it survives the redraws
// that follow an action, and is reset in open() so the menu never comes up
// scrolled from last time.
uint8_t menuTop = 0;
```

Then replace `drawMenu`:

```cpp
void drawMenu(uint8_t index, bool partial) {
  // Keep the highlighted row inside the window, and never scroll past the end.
  if (index < menuTop) {
    menuTop = index;
  } else if (index >= menuTop + MENU_VISIBLE) {
    menuTop = (uint8_t)(index - MENU_VISIBLE + 1);
  }
  if (ITEM_COUNT > MENU_VISIBLE && menuTop > (uint8_t)(ITEM_COUNT - MENU_VISIBLE)) {
    menuTop = (uint8_t)(ITEM_COUNT - MENU_VISIBLE);
  } else if (ITEM_COUNT <= MENU_VISIBLE) {
    menuTop = 0;
  }

  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);

  char label[24];
  int16_t x1, y1;
  uint16_t w, h;
  for (uint8_t slot = 0; slot < MENU_VISIBLE && menuTop + slot < ITEM_COUNT;
       slot++) {
    const uint8_t i = (uint8_t)(menuTop + slot);
    itemLabel(i, label, sizeof(label));
    const int16_t yPos = MENU_ROW_H + (MENU_ROW_H * slot);
    display.setCursor(0, yPos);
    if (i == index) {
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

- [ ] **Step 2: Reset the window when the menu opens**

In `open()`, the menu should always come up at the top. Add the reset next to the index:

```cpp
  uint8_t index = 0;
  menuTop = 0;
  drawMenu(index, false);
```

- [ ] **Step 3: Build all four envs**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines. With `ITEM_COUNT` still 7 the window never scrolls, so this is a no-op change on the current menu — which is the point: it lands and is verified before the new entries depend on it.

- [ ] **Step 4: Commit**

```bash
git add RefCounter/RefMenu.cpp
git commit -m "feat: scroll the settings menu so it can hold more than seven entries"
```

---

### Task 6: The Sport picker

Add the `Sport: <name>` menu entry and the picker screen behind it.

**Files:**
- Modify: `RefCounter/RefMenu.cpp` (`Item` enum, `itemLabel`, a new `pickSport`, the dispatch in `open`)

**Interfaces:**
- Consumes: `RefSport::count()`, `preset()`, `index()`, `setIndex()` from Task 3; the scrolling `drawMenu` from Task 5.
- Produces: `ITEM_SPORT` at the head of the `Item` enum. Task 7 adds `ITEM_EDIT_CUSTOM` directly after it.

- [ ] **Step 1: Include RefSport and add the menu entry**

In `RefCounter/RefMenu.cpp`, add the include:

```cpp
#include "RefPanel.h"
#include "RefSport.h"
#include "RefZone.h"
```

Put the sport at the head of the enum — it is the entry an official opens the menu for on a Saturday, and everything below it is set-once configuration:

```cpp
enum Item : uint8_t {
  ITEM_SPORT,
  ITEM_ABOUT,
  ITEM_BUZZ,
  ITEM_SET_TIME,
  ITEM_ZONE,
  ITEM_DST,
  ITEM_WIFI,
  ITEM_SYNC,
  ITEM_COUNT,
};
```

And its label, which reads its current value the way `TZ` and `DST` already do. Update the comment above `itemLabel` too, since the widest row is no longer `TZ: Mountain`:

```cpp
// Three of these read their current value rather than being fixed text, so the
// menu doubles as the status display for the sport, the zone and the DST
// switch. The widest this gets is "Sport: Base NCAA", 16 glyphs.
void itemLabel(uint8_t i, char *buf, size_t n) {
  switch (i) {
  case ITEM_SPORT:    snprintf(buf, n, "Sport: %s", RefSport::active().name); break;
  case ITEM_ABOUT:    snprintf(buf, n, "About"); break;
  case ITEM_BUZZ:     snprintf(buf, n, "Vibrate Motor"); break;
  case ITEM_SET_TIME: snprintf(buf, n, "Set Time"); break;
  case ITEM_ZONE:     snprintf(buf, n, "TZ: %s", RefZone::name(RefZone::index())); break;
  case ITEM_DST:      snprintf(buf, n, "DST: %s", RefZone::dstAuto() ? "Auto" : "Off"); break;
  case ITEM_WIFI:     snprintf(buf, n, "Setup WiFi"); break;
  case ITEM_SYNC:     snprintf(buf, n, "Sync NTP"); break;
  default:            snprintf(buf, n, "?"); break;
  }
}
```

- [ ] **Step 2: Write the picker**

Add `pickSport` to `RefCounter/RefMenu.cpp`, directly above `pickTimeZone` — the two are deliberately the same shape, and reading them next to each other is the point.

```cpp
// The sport picker. Same shape as the zone picker below: a scrolling window of
// rows, MENU to take the highlighted one, BACK to leave the stored one alone,
// and a timeout so a menu opened by accident cannot strand anyone mid-game.
void pickSport() {
  const int16_t ROW_H   = 22;
  const uint8_t VISIBLE = 7;
  const int16_t FIRST_Y = 22;
  const int16_t RULE_Y  = 164;
  const uint8_t total   = RefSport::count();

  uint8_t index = RefSport::index();
  uint8_t top   = index < VISIBLE ? 0 : (uint8_t)(index - VISIBLE + 1);

  claimButtons();
  display.setFullWindow();

  bool first = true;
  uint32_t lastActivity = millis();
  while (millis() - lastActivity < MENU_TIMEOUT_MS) {
    if (index < top) {
      top = index;
    } else if (index >= top + VISIBLE) {
      top = (uint8_t)(index - VISIBLE + 1);
    }

    display.fillScreen(THEME_BG);
    display.setFont(&FreeMonoBold9pt7b);

    // "Base NCAA 120/20" is the widest row, 16 glyphs of the 18 that fit.
    char row[24];
    for (uint8_t slot = 0; slot < VISIBLE && top + slot < total; slot++) {
      const uint8_t s       = (uint8_t)(top + slot);
      const int16_t yPos    = FIRST_Y + ROW_H * slot;
      const RefSport::Preset p = RefSport::preset(s);
      snprintf(row, sizeof(row), "%-9s %u/%u", p.name, (unsigned)p.longSeconds,
               (unsigned)p.shortSeconds);

      if (s == index) {
        display.fillRect(0, yPos - 17, DISPLAY_WIDTH, ROW_H - 1, THEME_FG);
        display.setTextColor(THEME_BG);
      } else {
        display.setTextColor(THEME_FG);
      }
      display.setCursor(2, yPos);
      display.print(row);
    }

    display.setTextColor(THEME_FG);
    display.drawFastHLine(0, RULE_Y, DISPLAY_WIDTH, THEME_FG);

    const RefSport::Preset sel = RefSport::preset(index);

    display.setCursor(2, 180);
    display.print(sel.description);

    char counter[10];
    snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(index + 1),
             (unsigned)total);
    // FreeMonoBold9pt7b advances 11 pixels a glyph, so right-aligning is
    // arithmetic rather than a getTextBounds round trip.
    display.setCursor(DISPLAY_WIDTH - 2 - (int16_t)(strlen(counter) * 11), 180);
    display.print(counter);

    // The buzz marks, which are the half of a preset the rows have no room
    // for. "warn 30/10 last 5" is 17 glyphs.
    char marks[24];
    snprintf(marks, sizeof(marks), "warn %u/%u last %u",
             (unsigned)sel.warnAtSeconds, (unsigned)sel.warn2AtSeconds,
             (unsigned)sel.finalCountdownFrom);
    display.setCursor(2, 196);
    display.print(marks);

    display.display(!first); // full refresh on the way in, partial to scroll
    first = false;

    while (millis() - lastActivity < MENU_TIMEOUT_MS) {
      if (pressed(PIN_BTN_MENU)) {
        RefSport::setIndex(index);
        Buzzer::pulse(BUZZ_CONFIRM_MS);
        Buttons::waitForRelease();
        return;
      }
      if (pressed(PIN_BTN_BACK)) {
        Buttons::waitForRelease();
        return; // leave the stored sport alone
      }
      if (pressed(PIN_BTN_UP)) {
        index = (index == 0) ? (uint8_t)(total - 1) : (uint8_t)(index - 1);
        lastActivity = millis();
        break;
      }
      if (pressed(PIN_BTN_DOWN)) {
        index = (uint8_t)((index + 1) % total);
        lastActivity = millis();
        break;
      }
      delay(BUTTON_POLL_MS);
    }
  }
}
```

- [ ] **Step 3: Dispatch it**

In `open()`, add the case at the head of the switch, matching the enum order:

```cpp
      switch (index) {
      case ITEM_SPORT:    pickSport(); break;
      case ITEM_ABOUT:    showAbout(refClock); holdResult = true; break;
```

- [ ] **Step 4: Build all four envs**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines. `ITEM_COUNT` is now 8, so `drawMenu` scrolls for real — the row that scrolls off the bottom is `Sync NTP`, reachable by pressing DOWN past `Setup WiFi`.

- [ ] **Step 5: Commit**

```bash
git add RefCounter/RefMenu.cpp
git commit -m "feat: pick a sport preset from the settings menu"
```

---

### Task 7: The Custom editor

Add the `Edit Custom` entry and the five-field screen behind it.

**Files:**
- Modify: `RefCounter/RefMenu.cpp` (`Item` enum, `itemLabel`, a new `editCustom`, the dispatch in `open`)

**Interfaces:**
- Consumes: `RefSport::custom()`, `RefSport::setCustom()`, `RefSport::MAX_SECONDS`, `RefSport::MIN_CLOCK_SECONDS` from Task 3.
- Produces: `ITEM_EDIT_CUSTOM`, second in the `Item` enum. Nothing after this task consumes it.

- [ ] **Step 1: Add the menu entry**

In `RefCounter/RefMenu.cpp`, add to the enum right after `ITEM_SPORT`:

```cpp
enum Item : uint8_t {
  ITEM_SPORT,
  ITEM_EDIT_CUSTOM,
  ITEM_ABOUT,
  ITEM_BUZZ,
  ITEM_SET_TIME,
  ITEM_ZONE,
  ITEM_DST,
  ITEM_WIFI,
  ITEM_SYNC,
  ITEM_COUNT,
};
```

and to `itemLabel`, after the `ITEM_SPORT` case:

```cpp
  case ITEM_EDIT_CUSTOM: snprintf(buf, n, "Edit Custom"); break;
```

- [ ] **Step 2: Write the editor**

Add to `RefCounter/RefMenu.cpp`, directly below `setTime` — the interaction is deliberately identical, and the two belong together.

```cpp
enum CustomField : int8_t {
  CF_LONG,
  CF_SHORT,
  CF_WARN,
  CF_WARN2,
  CF_FINAL,
  CF_COUNT,
};

const char *customFieldLabel(int8_t f) {
  switch (f) {
  case CF_LONG:  return "Long";
  case CF_SHORT: return "Short";
  case CF_WARN:  return "Warn 1";
  case CF_WARN2: return "Warn 2";
  default:       return "Final";
  }
}

// Edit the Custom preset's five numbers. MENU steps forward through the fields
// and commits past the last one, BACK steps back, UP and DOWN change the value
// under the cursor, and the field being edited blinks -- the same interaction
// as setTime above, deliberately.
//
// Values are text rather than seven-segment digits: five labelled rows are far
// more readable than five bare numbers, and it keeps new segment geometry out
// of the menu.
//
// This does not select Custom. The user picks it from the Sport screen, which
// keeps "edit" and "use" separate the way the zone picker keeps them.
void editCustom() {
  const RefSport::Preset c = RefSport::custom();
  uint16_t value[CF_COUNT] = {c.longSeconds, c.shortSeconds, c.warnAtSeconds,
                              c.warn2AtSeconds, c.finalCountdownFrom};

  int8_t field = CF_LONG;
  bool   blink = false;

  claimButtons();
  display.setFullWindow();

  // No delay in this loop and no debounce: the partial refresh at the bottom
  // takes most of half a second, which is what paces it. Same as setTime.
  while (true) {
    if (pressed(PIN_BTN_MENU)) {
      field++;
      if (field >= CF_COUNT) {
        break;
      }
    }
    if (pressed(PIN_BTN_BACK) && field != CF_LONG) {
      field--;
    }

    blink = !blink;

    const int delta = pressed(PIN_BTN_DOWN) ? 1 : (pressed(PIN_BTN_UP) ? -1 : 0);
    if (delta != 0) {
      blink = true; // never hide the field the user is actively changing
      // The two clocks bottom out at 1; a mark of 0 means off, so those wrap
      // through zero.
      const int lo = (field == CF_LONG || field == CF_SHORT)
                         ? (int)RefSport::MIN_CLOCK_SECONDS
                         : 0;
      int v = (int)value[field] + delta;
      if (v > (int)RefSport::MAX_SECONDS) {
        v = lo;
      } else if (v < lo) {
        v = (int)RefSport::MAX_SECONDS;
      }
      value[field] = (uint16_t)v;
    }

    display.fillScreen(THEME_BG);
    display.setFont(&FreeMonoBold9pt7b);

    display.setTextColor(THEME_FG);
    display.setCursor(2, 20);
    display.print("EDIT CUSTOM");
    display.drawFastHLine(0, 28, DISPLAY_WIDTH, THEME_FG);

    for (int8_t f = 0; f < CF_COUNT; f++) {
      const int16_t yPos = 52 + 24 * f;

      display.setTextColor(THEME_FG);
      display.setCursor(2, yPos);
      display.print(customFieldLabel(f));

      char num[8];
      snprintf(num, sizeof(num), "%3u", (unsigned)value[f]);
      // Three glyphs at 11 pixels each, right-aligned against the edge.
      display.setCursor(DISPLAY_WIDTH - 2 - 33, yPos);
      display.setTextColor(f == field && !blink ? THEME_BG : THEME_FG);
      display.print(num);
    }

    display.setTextColor(THEME_FG);
    display.setCursor(2, 194);
    display.print("MENU/BACK to move");

    display.display(true); // partial refresh
  }

  RefSport::setCustom(value[CF_LONG], value[CF_SHORT], value[CF_WARN],
                      value[CF_WARN2], value[CF_FINAL]);
  Buzzer::pulse(BUZZ_CONFIRM_MS);
}
```

- [ ] **Step 3: Dispatch it**

In `open()`, add the case after `ITEM_SPORT`:

```cpp
      switch (index) {
      case ITEM_SPORT:       pickSport(); break;
      case ITEM_EDIT_CUSTOM: editCustom(); break;
      case ITEM_ABOUT:       showAbout(refClock); holdResult = true; break;
```

- [ ] **Step 4: Build all four envs**

Run:

```bash
pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four `SUCCESS` lines, and no `-Wswitch` warning about an unhandled enumerator — that would mean a case was missed in either `itemLabel` or `open`.

- [ ] **Step 5: Re-run the host tests**

Run:

```bash
./tests/run.sh
```

Expected: four tests, all `PASSED (0 failures)`. Nothing in this task touches tested code, so a failure here means something upstream regressed.

- [ ] **Step 6: Commit**

```bash
git add RefCounter/RefMenu.cpp
git commit -m "feat: edit the custom sport preset from the menu"
```

---

### Task 8: Documentation

The README is the only documentation this project has, and five of its sections now describe firmware that no longer exists. This is its own task because it spans every task above.

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Rewrite the opening description and Controls**

The subtitle "A purpose-built play clock for officiating football" is no longer accurate. Change it to cover the sports the preset table ships, and note in the Controls table that the two right-hand buttons run the active preset's long and short clocks rather than fixed 40 and 25.

- [ ] **Step 2: Rewrite "Buzz pattern"**

The table is stated for a 40 second clock with fixed marks. Restate it against the active preset — first warning, second warning, final countdown, expire — and add the preset table from the spec so a reader can see what each sport buzzes on. Note that ReadyRef quote pre-warnings in elapsed seconds and this firmware uses remaining, with the 40s/30s-elapsed/10s-remaining worked example.

- [ ] **Step 3: Add a "Sports" section**

New section after "Controls", covering: the preset table, that the choice persists in NVS across a reflash, how to pick one (`Sport` in the menu), and how to edit `Custom` (`Edit Custom`, five fields, 1–199 for clocks and 0–199 for marks, 0 means off). State plainly that editing Custom does not select it.

- [ ] **Step 4: Update "Screens" and its menu table**

The menu table needs two new rows — `Sport: name` and `Edit Custom` — with the same "shows its current value" note `TZ` and `DST` already carry. Mention that the menu now scrolls a seven-row window, and that the Ready screen's footer shows the active sport name instead of `HOLD TO START`.

- [ ] **Step 5: Update "Configuring"**

The sample block still lists `TIMER_LONG_SECONDS`, `TIMER_SHORT_SECONDS`, `WARNING_AT_SECONDS` and `FINAL_COUNTDOWN_FROM`, none of which exist. The first four lines of that block become:

```c
static const char     DEFAULT_SPORT[]      = "Football"; // until set in the menu
static const uint16_t CUSTOM_LONG_SECONDS  = 40;  // the Custom slot's factory
static const uint16_t CUSTOM_SHORT_SECONDS = 25;  //   values, likewise only a
static const uint16_t CUSTOM_WARN_SECONDS  = 10;  //   starting point
static const uint16_t CUSTOM_WARN2_SECONDS = 0;   // 0 = off
static const uint16_t CUSTOM_FINAL_FROM    = 5;
static const uint32_t TIMER_HOLD_MS        = 500;  // hold to start / reset
static const uint32_t SLEEP_HOLD_MS        = 1000; // hold for low power mode
static const uint32_t BUZZ_EXPIRE_MS       = 1000; // buzz at zero
static const uint8_t  WARNING_BUZZ_COUNT   = 1;    // buzzes at the first mark
static const uint8_t  WARNING_BUZZ_COUNT_2 = 2;    // buzzes at the second
```

with the rest of the block (`DARK_MODE` onward) unchanged. Say beneath it that the clock lengths themselves moved to the on-watch menu.

The paragraph "There is no on-device settings menu — one fewer thing to fat-finger during a game" is now false; the timing values are editable on the watch. Replace it with the honest version: they are behind a menu that needs a deliberate hold from the Ready screen and times out on its own, and the six fixed presets cannot be edited at all.

"Clock values are limited to 1–99, because the display shows two digits" becomes 1–199, with the leading-`1` explanation.

- [ ] **Step 6: Update "Repo structure" and "Design notes"**

Add two lines to the tree, in the order the existing entries follow:

```
    ├── RefSegments.h/.cpp  where the countdown digits land, host-tested
    ├── RefSport.h/.cpp     sport presets and the custom slot
```

In the refresh-strategy table, the `renderDigits()` row's area is no longer `176x124`:

| Call | Area | Cost | Used for |
| --- | --- | --- | --- |
| `renderDigits()` | 200x124 window | ~280ms | every second of a running clock |

and the sentence below it — "so only 54% of the panel is driven" — becomes 62%. Note that the window widened to cover the hundreds bar, and that the extra cost is the price of never leaving a stale `1` on the panel.

Under **Digits are drawn, not typed**, the claim that the partial window is safe to hard-code now rests on `RefSegments::layoutCount` rather than on the digits being exactly two.

- [ ] **Step 7: Update "Testing"**

The section describes two tests over `RefZone.cpp` only. It now runs four, over three source files. Describe what `segments_test` and `sport_test` cover, and mention that the `Preferences` stub gained a switch so persistence itself can be tested.

- [ ] **Step 8: Check the README against the firmware**

Run:

```bash
grep -n "TIMER_LONG_SECONDS\|TIMER_SHORT_SECONDS\|WARNING_AT_SECONDS\|FINAL_COUNTDOWN_FROM\|1–99\|1-99\|176x124\|no on-device settings menu" README.md
```

Expected: no output. Every hit is a claim about firmware that no longer exists.

- [ ] **Step 9: Commit**

```bash
git add README.md
git commit -m "docs: cover sport presets, the custom slot and the new tests"
```

---

## Verification

After Task 8, from a clean tree:

```bash
./tests/run.sh && pio run -e watchy_v2 -e watchy_v15 -e watchy_v10 -e watchy_v3
```

Expected: four host tests `PASSED (0 failures)`, then four `SUCCESS` lines.

**What this does not prove.** No part of this has run on a watch. In particular the following are untested by anything and are the first things to check when hardware is available:

- Whether the leading `1` actually clears from the panel when a clock crosses 100 → 99. The widened refresh window is the fix; only a real panel confirms it.
- Whether nine menu rows scroll legibly, and whether the highlight bar lands correctly on the seventh row.
- Whether the Custom editor's five rows fit without the descenders clipping at y 148.
- Whether NVS survives a reflash for the `refsport` namespace as it reportedly does for `refzone`.
