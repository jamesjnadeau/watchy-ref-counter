# Remove Watchy V1.0 / V1.5 Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drop Watchy V1.0 and V1.5 from the firmware, leaving V2.0 (ESP32, PCF8563) and V3 (ESP32-S3, internal clock) as the only supported revisions.

**Architecture:** This is a deletion, not a refactor. Three things get simpler: `board.h`'s ESP32 branch collapses from a three-way `#if` to one unconditional V2.0 pin map, `RefRtc` loses the DS3231 driver it carried only for V1.0, and `RefClock::boardRevision()` stops needing a build-time `#if` to tell V1.5 from V2.0. A `#error` guard replaces the removed revision flags so a stale `-D ARDUINO_WATCHY_V10` fails loudly instead of silently flashing a V2.0 pin map onto v1 hardware. New host tests pin the surviving pin map and prove the guard fires.

**Tech Stack:** C++17, Arduino/ESP32 core, PlatformIO, GxEPD2. Host tests are plain `g++` binaries driven by `tests/run.sh`.

**Spec:** [`docs/superpowers/specs/2026-08-19-remove-watchy-v1.md`](../specs/2026-08-19-remove-watchy-v1.md)

## Global Constraints

- **Supported revisions after this work:** V2.0 (`watchy_v2`, the default env, ESP32, PCF8563 @ 0x51) and V3 (`watchy_v3`, ESP32-S3, internal 32kHz-backed clock). Nothing else.
- **Every task ends with both PlatformIO envs building:** `pio run -e watchy_v2 -e watchy_v3`.
- **Every task ends with the host suite passing:** `./tests/run.sh`.
- **Guard message, verbatim, in `board.h`:** `Watchy V1.0 and V1.5 are no longer supported; build watchy_v2 or watchy_v3`
- **V2.0 pin numbers are facts, not choices.** They do not change in this work: `PIN_BTN_UP 35`, `PIN_BATT_ADC 34`, `PIN_BTN_MENU 26`, `PIN_BTN_BACK 25`, `PIN_BTN_DOWN 4`, `PIN_I2C_SDA 21`, `PIN_I2C_SCL 22`, `PIN_SPI_SCK 18`, `PIN_SPI_MOSI 23`, `PIN_DISPLAY_CS 5`, `PIN_DISPLAY_RST 9`, `PIN_DISPLAY_DC 10`, `PIN_DISPLAY_BUSY 19`, `PIN_VIB_MOTOR 13`, `BTN_PRESSED_LEVEL HIGH`.
- **The working tree has unrelated in-progress changes** under `board-files/`, plus modifications to `README.md` and `RefCounter/`. Do **not** run `git add -A` or `git commit -a`. Every commit step below lists exact paths; use those.
- **Comment style:** this codebase explains *why*, in full sentences, in the file the reader will be in. Match it. Do not leave a bare `// removed V1` marker anywhere.

---

## File Structure

**Modified:**

| File | Responsibility after this work |
| --- | --- |
| `platformio.ini` | Two envs: `watchy_v2` (default) and `watchy_v3` |
| `RefCounter/board.h` | One ESP32 pin map (V2.0) + one S3 pin map (V3), plus the `#error` guard for retired flags |
| `RefCounter/RefRtc.h` | `Kind` = `{NONE, PCF8563, INTERNAL}`; no DS3231 declarations |
| `RefCounter/RefRtc.cpp` | PCF8563 probe/read/write and the S3 internal clock; no DS3231 code |
| `RefCounter/RefClock.h` | `boardRevision()` doc comment: 20 / 30 / 0 |
| `RefCounter/RefClock.cpp` | `boardRevision()` without the build-time `#if` |
| `RefCounter/RefMenu.cpp` | About screen's RTC label, minus the DS3231 case |
| `RefCounter/settings.h` | Drift comment referencing the PCF8563 in V2 only |
| `README.md` | Build, timekeeping, repo-structure and limitations sections describing two revisions |
| `tests/run.sh` | Runs the new `board_test`, plus a negative-compile check for the guard |

**Created:**

| File | Responsibility |
| --- | --- |
| `tests/stub/Arduino.h` | Minimal host stub so `board.h` and `RefRtc.h` compile off-target |
| `tests/board_test.cpp` | `static_assert`s pinning the V2.0 pin map and the surviving `RefRtc::Kind` values |

The two new test files exist because the pin map and the RTC-kind enum are the only things in this change a reader could silently break later. Everything else in the change is a deletion that the compiler catches.

---

## Task 1: Retire the v1 build envs and pin map

Removes the `watchy_v10` / `watchy_v15` envs and the per-revision `#if` in `board.h`, and makes the retired flags a build error. Ships with the host-side scaffolding (`tests/stub/Arduino.h`, `tests/board_test.cpp`) that both this task and Task 2 assert against.

**Files:**
- Modify: `platformio.ini:1-46`
- Modify: `RefCounter/board.h:1-35` (header comment and revision selection), `RefCounter/board.h:79-110` (ESP32 branch)
- Create: `tests/stub/Arduino.h`
- Create: `tests/board_test.cpp`
- Modify: `tests/run.sh:1-40`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `board.h` exports the same macro names it does today — `PIN_I2C_SDA`, `PIN_I2C_SCL`, `PIN_SPI_SCK`, `PIN_SPI_MOSI`, `PIN_SPI_MISO` (V3 only), `PIN_SPI_SS` (V3 only), `PIN_BTN_MENU`, `PIN_BTN_BACK`, `PIN_BTN_UP`, `PIN_BTN_DOWN`, `PIN_DISPLAY_CS`, `PIN_DISPLAY_DC`, `PIN_DISPLAY_RST`, `PIN_DISPLAY_BUSY`, `PIN_VIB_MOTOR`, `PIN_BATT_ADC`, `BTN_PRESSED_LEVEL`, `BTN_EXT1_WAKE_MODE`, `BTN_LIGHT_SLEEP_WAKE_LEVEL`, `BATT_DIVIDER`, `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`, `BATT_MIN_V`, `BATT_MAX_V`, `BTN_LONG_TIMER_PIN`, `BTN_SHORT_TIMER_PIN`, `BTN_SLEEP_PIN`, `BTN_RESET_PIN`. No macro is renamed or removed; only the V1.0/V1.5 *values* disappear.
- Produces: `tests/stub/Arduino.h` defines `LOW` (0x0) and `HIGH` (0x1) and includes `<cstdint>`. Task 2 includes it transitively via `RefRtc.h`.
- Produces: `tests/run.sh` gains a `board_test` entry with an empty source list, and a `board_guard` block after the main loop.

- [ ] **Step 1: Create the host stub for `Arduino.h`**

`board.h` includes `<Arduino.h>` and takes exactly two things from it: the `LOW` and `HIGH` pin levels used by `BTN_PRESSED_LEVEL`. Everything else it references (`ESP_EXT1_WAKEUP_ANY_HIGH`, `GPIO_INTR_HIGH_LEVEL`) sits inside `#define` bodies that are never expanded on the host, so the stub does not need them.

Create `tests/stub/Arduino.h`:

```c
#pragma once

// Host stub for the ESP32 core's Arduino.h.
//
// board.h is the only header the host tests pull in that needs it, and all it
// takes from Arduino.h are the two pin levels. board.h's other core
// references -- the ext1 wake mode and the light sleep level -- sit inside
// #define bodies that nothing on the host expands, so they are not needed
// here. Keep this file that small: it is a stub, not a second core.

#include <cstdint>

#define LOW  0x0
#define HIGH 0x1
```

- [ ] **Step 2: Write the pin-map test**

This one passes before the change as well as after — the V2.0 numbers are what a flagless build already selects. It is here so that collapsing the `#if` in Step 5 is provably a no-op for V2.0, and so a later edit that moves a pin fails a test instead of a watch.

Create `tests/board_test.cpp`:

```cpp
// The pin map is a fact about the hardware, not a preference, so it gets a
// test that fails loudly if a define moves. These are the V2.0 numbers.
//
// V1.0 and V1.5 are no longer supported and board.h #errors on their flags.
// That half is checked by tests/run.sh, because a compile that has to fail
// cannot live in a file that has to compile.

#define ARDUINO_WATCHY_V20 // as platformio.ini's watchy_v2 env does

#include <stdio.h>

#include "board.h"

static_assert(PIN_BTN_MENU == 26, "V2.0 menu button is GPIO 26");
static_assert(PIN_BTN_BACK == 25, "V2.0 back button is GPIO 25");
static_assert(PIN_BTN_DOWN == 4, "V2.0 down button is GPIO 4");
static_assert(PIN_BTN_UP == 35, "V2.0 up button is GPIO 35, not V1's 32");
static_assert(PIN_BATT_ADC == 34, "V2.0 battery tap is GPIO 34, not V1.0's 33 "
                                  "or V1.5's 35");
static_assert(PIN_I2C_SDA == 21 && PIN_I2C_SCL == 22, "V2.0 I2C pins");
static_assert(PIN_SPI_SCK == 18 && PIN_SPI_MOSI == 23, "V2.0 SPI pins");
static_assert(PIN_DISPLAY_CS == 5 && PIN_DISPLAY_RST == 9 &&
                  PIN_DISPLAY_DC == 10 && PIN_DISPLAY_BUSY == 19,
              "V2.0 panel control pins");
static_assert(PIN_VIB_MOTOR == 13, "V2.0 vibration motor is GPIO 13");
static_assert(BTN_PRESSED_LEVEL == HIGH,
              "the ESP32 revisions read high when a button is pressed");

int main() {
  printf("board: V2.0 pin map ok\n");
  return 0;
}
```

- [ ] **Step 3: Wire both tests into the runner**

Two edits to `tests/run.sh`. First, add `board_test` to the table and the loop — it needs no source files of its own, so its entry is an empty string:

```bash
declare -A SOURCES=(
  [tz_test]="RefCounter/RefZone.cpp"
  [tz_edges]="RefCounter/RefZone.cpp"
  [segments_test]="RefCounter/RefSegments.cpp"
  [sport_test]="RefCounter/RefSport.cpp"
  [sync_schedule]="RefCounter/RefSyncSchedule.cpp"
  [wifi_test]="RefCounter/RefWifi.cpp"
  [menu_items_test]="RefCounter/RefMenuItems.cpp RefCounter/RefSport.cpp RefCounter/RefZone.cpp"
  [board_test]=""
)

for t in tz_test tz_edges segments_test sport_test sync_schedule wifi_test menu_items_test board_test; do
```

Second, append this block at the end of the file, after the loop:

```bash
# board.h has to refuse the retired V1.0 and V1.5 revision flags outright. A
# leftover -D or an Arduino IDE user's stale #define must stop the build, not
# quietly select the V2.0 map -- on that hardware the battery tap and the up
# button are on other pins entirely. This is a compile that has to fail, which
# is why it lives here rather than in a .cpp.
echo "=== board_guard"
for flag in ARDUINO_WATCHY_V10 ARDUINO_WATCHY_V15; do
  if err=$($CXX -std=c++17 -Itests/stub -IRefCounter -fsyntax-only \
                -D "$flag" -xc++ - <<<'#include "board.h"' 2>&1); then
    echo "FAIL: board.h accepted -D $flag" >&2
    exit 1
  fi
  case "$err" in
    *"no longer supported"*) echo "  -D $flag rejected" ;;
    *) echo "FAIL: -D $flag failed, but not on the guard:" >&2
       echo "$err" >&2
       exit 1 ;;
  esac
done
echo
```

Also update the comment block at the top of `tests/run.sh` so the file list matches. Replace the first paragraph's file list line:

```bash
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp
# and RefMenuItems.cpp are the files with logic worth testing off the watch: no
```

with:

```bash
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp
# and RefMenuItems.cpp are the files with logic worth testing off the watch,
# and board.h's pin map is checked here too. Between them: no
```

- [ ] **Step 4: Run the tests and watch the guard check fail**

Run: `./tests/run.sh`

Expected: `board_test` prints `board: V2.0 pin map ok` and passes. Then `board_guard` fails with `FAIL: board.h accepted -D ARDUINO_WATCHY_V10`, and the script exits non-zero. That is the red test — `board.h` has no guard yet.

- [ ] **Step 5: Rewrite the revision selection in `board.h`**

Replace lines 12-35 of `RefCounter/board.h` — from the `// The revision is selected by the build:` comment through the closing `#endif` of the no-revision fallback — with:

```c
// The revision is selected by the build: ARDUINO_WATCHY_V20 for the ESP32
// board, ARDUINO_ESP32S3_DEV for V3. See platformio.ini.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// PlatformIO sets the revision from platformio.ini. The Arduino IDE has no
// build-flag field, so IDE users on a V2.0 uncomment the line below -- it has
// to live in this header, which every source file includes, rather than in
// the .ino, which only sets it for itself.
//
// #define ARDUINO_WATCHY_V20
//
// V3 needs nothing: selecting an ESP32-S3 board defines ARDUINO_ESP32S3_DEV.

// V1.0 and V1.5 are no longer supported. Their up button and battery tap sat
// on other pins than V2.0's -- 32/33 on V1.0 and 32/35 on V1.5, against
// 35/34 here -- so quietly building the V2.0 map for one of them would read
// the battery off the wrong pin and never see the up button at all. A stale
// build flag or an old #define left in this file therefore stops the build.
#if defined(ARDUINO_WATCHY_V10) || defined(ARDUINO_WATCHY_V15)
#error "Watchy V1.0 and V1.5 are no longer supported; build watchy_v2 or watchy_v3"
#endif

#if !defined(ARDUINO_ESP32S3_DEV) && !defined(ARDUINO_WATCHY_V20)
#warning "No Watchy revision defined; assuming V2.0"
#define ARDUINO_WATCHY_V20
#endif
```

Note the `// ------` rule and `#include <Arduino.h>` in that block are the existing lines 14-16; keep them where they are rather than duplicating them.

- [ ] **Step 6: Collapse the ESP32 pin map to V2.0**

Replace the ESP32 branch of `RefCounter/board.h` — the `#else` block that today opens `// --- V1.0 / V1.5 / V2.0 (ESP32) ---` and runs to just before `// Buttons pull their pin high when pressed.` — with:

```c
#else
// --- V2.0 (ESP32) ----------------------------------------------------------
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 23

#define PIN_BTN_MENU 26
#define PIN_BTN_BACK 25
#define PIN_BTN_DOWN 4
#define PIN_BTN_UP   35

#define PIN_DISPLAY_CS   5
#define PIN_DISPLAY_RST  9
#define PIN_DISPLAY_DC   10
#define PIN_DISPLAY_BUSY 19

#define PIN_VIB_MOTOR 13
#define PIN_BATT_ADC  34

```

The lines that follow — `// Buttons pull their pin high when pressed.`, the three `BTN_*` defines, the divider comment and `#define BATT_DIVIDER 2.0f`, and the closing `#endif` — stay exactly as they are.

- [ ] **Step 7: Run the tests and watch them pass**

Run: `./tests/run.sh`

Expected: every test passes, ending with

```
=== board_guard
  -D ARDUINO_WATCHY_V10 rejected
  -D ARDUINO_WATCHY_V15 rejected
```

and exit status 0.

- [ ] **Step 8: Remove the v1 envs from `platformio.ini`**

Delete these two blocks entirely:

```ini
[env:watchy_v15]
board = esp32dev
build_flags = -D ARDUINO_WATCHY_V15

[env:watchy_v10]
board = esp32dev
build_flags = -D ARDUINO_WATCHY_V10
```

Then update the two comment blocks that name them. Replace the file's opening comment:

```ini
; Watchy Ref Counter
;
; The sketch lives in RefCounter/ so the same folder works unchanged as an
; Arduino IDE sketch (RefCounter/RefCounter.ino) and as the PlatformIO source
; directory. Pick the env that matches your hardware:
;
;   pio run -e watchy_v2 -t upload      (default)
;   pio run -e watchy_v3 -t upload
```

with:

```ini
; Watchy Ref Counter
;
; The sketch lives in RefCounter/ so the same folder works unchanged as an
; Arduino IDE sketch (RefCounter/RefCounter.ino) and as the PlatformIO source
; directory. Pick the env that matches your hardware:
;
;   pio run -e watchy_v2 -t upload      (default; V2.0, ESP32)
;   pio run -e watchy_v3 -t upload      (V3, ESP32-S3)
;
; V1.0 and V1.5 are not supported; board.h stops the build if their revision
; flags are set.
```

and replace the ESP32 section header:

```ini
; --- ESP32 revisions -------------------------------------------------------
; Generic ESP32 boards; the pin map is this project's own, in board.h. The
; revision define is what selects between them.
```

with:

```ini
; --- ESP32 revision --------------------------------------------------------
; A generic ESP32 board; the pin map is this project's own, in board.h. The
; revision define is what selects it over the S3 map.
```

- [ ] **Step 9: Verify both surviving envs build and the retired ones are gone**

Run: `pio run -e watchy_v2 -e watchy_v3`
Expected: two `SUCCESS` lines.

Run: `pio run -e watchy_v10`
Expected: failure — `Unknown environment names 'watchy_v10'`, listing `watchy_v2, watchy_v3` as the valid ones.

Clear the stale build directories so nothing later picks them up:

```bash
rm -rf .pio/build/watchy_v10 .pio/build/watchy_v15
```

- [ ] **Step 10: Commit**

```bash
git add platformio.ini RefCounter/board.h tests/stub/Arduino.h tests/board_test.cpp tests/run.sh
git commit -m "board: drop the V1.0 and V1.5 revisions

Their up button and battery tap sat on different pins from V2.0's, which
is the only reason board.h's ESP32 branch had a three-way #if. Neither
revision has ever been run against this firmware. The retired flags now
stop the build rather than silently selecting the V2.0 map, and the
surviving pin map is pinned by a host test."
```

---

## Task 2: Remove the DS3231 driver

The DS3231 was fitted only to V1.0. With that revision gone, `RefRtc` needs to know about the PCF8563 (V2.0) and the S3's internal clock (V3), and nothing else. `RefClock::boardRevision()` loses the build-time `#if` it needed to tell V1.5 from V2.0, since both shipped the same chip.

**Files:**
- Modify: `RefCounter/RefRtc.h:8-47`
- Modify: `RefCounter/RefRtc.cpp:10-11` (addresses), `:56-78` (`begin`), `:80-95` (`readDS3231`), `:115-125` (`read`), `:155-166` (`set`)
- Modify: `RefCounter/RefClock.h:62-64`
- Modify: `RefCounter/RefClock.cpp:192-213`
- Modify: `RefCounter/RefMenu.cpp:196-202`
- Modify: `tests/board_test.cpp`

**Interfaces:**
- Consumes: `tests/stub/Arduino.h` and `tests/board_test.cpp` from Task 1; the runner entry for `board_test` already exists.
- Produces: `RefRtc::Kind` is `enum Kind : uint8_t { NONE, PCF8563, INTERNAL }` — values 0, 1, 2. `RefRtc`'s public surface is otherwise unchanged: `void begin()`, `Kind kind() const`, `bool read(struct tm &out)`, `bool set(const struct tm &t)`, `time_t epoch()`.
- Produces: `uint8_t RefClock::boardRevision()` returns 30 (S3 / V3), 20 (ESP32 with a PCF8563 found) or 0 (no clock found).

- [ ] **Step 1: Write the failing test**

Append to `tests/board_test.cpp`, after the existing `static_assert`s and before `main()`:

```cpp
// The RTC kinds this firmware still knows about. DS3231 sat between NONE and
// PCF8563 and was fitted only to V1.0, so these values are what its removal
// leaves behind -- and asserting on them is the cheapest way to notice if it
// ever creeps back.
#include "RefRtc.h"

static_assert(RefRtc::NONE == 0, "no clock found");
static_assert(RefRtc::PCF8563 == 1, "V2.0's chip; the DS3231 slot is gone");
static_assert(RefRtc::INTERNAL == 2, "V3 keeps time in the SoC");
```

Update `main()` so a pass says what passed:

```cpp
int main() {
  printf("board: V2.0 pin map and RTC kinds ok\n");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./tests/run.sh`

Expected: the `board_test` compile fails with

```
static assertion failed: V2.0's chip; the DS3231 slot is gone
note: the comparison reduces to '(2 == 1)'
```

because `DS3231` is still occupying value 1.

- [ ] **Step 3: Drop DS3231 from `RefRtc.h`**

Replace the header's class comment and enum — lines 8 through 26 of `RefCounter/RefRtc.h`, from `// The watch's real time clock.` through the `Kind` enum — with:

```c
// The watch's real time clock.
//
// Which one that is depends on the revision, so this probes for what is
// actually on the bus:
//   V2.0  PCF8563  at 0x51
//   V3    none; the ESP32-S3 keeps time itself on a 32kHz crystal
//
// The chip is talked to directly over Wire in binary coded decimal. That is a
// handful of register reads, and doing it here keeps the project free of the
// RTC libraries the reference firmware pulls in -- one of which no longer
// compiles against a current ESP32 core.
//
// Credit for the chip choice and the address: sqfmi/Watchy.
class RefRtc {
public:
  enum Kind : uint8_t { NONE, PCF8563, INTERNAL };
```

Then delete this line from the private section:

```c
  bool readDS3231(struct tm &out);
```

- [ ] **Step 4: Drop DS3231 from `RefRtc.cpp`**

Four deletions and one edit.

Delete the address constant, leaving:

```c
const uint8_t ADDR_PCF8563 = 0x51;
```

Replace the non-S3 half of `begin()` — the `if (present(ADDR_DS3231)) { ... } else if (present(ADDR_PCF8563)) { ... }` chain — with:

```c
  if (present(ADDR_PCF8563)) {
    _kind = PCF8563;
    // Control 1 and 2 cleared: run normally, no alarm or timer interrupts.
    const uint8_t control[2] = {0x00, 0x00};
    writeRegisters(ADDR_PCF8563, 0x00, control, 2);
  } else {
    _kind = NONE;
  }
```

Delete the whole `readDS3231` function and the two-line register comment above it (`// DS3231 registers 0x00..0x06: ...`).

Delete this case from `read()`:

```c
  case DS3231:
    ok = readDS3231(out);
    break;
```

Delete this case from `set()`:

```c
  case DS3231: {
    const uint8_t r[7] = {
        toBcd((uint8_t)t.tm_sec),        toBcd((uint8_t)t.tm_min),
        toBcd((uint8_t)t.tm_hour),       // bit 6 clear selects 24 hour mode
        (uint8_t)(t.tm_wday + 1),        toBcd((uint8_t)t.tm_mday),
        toBcd((uint8_t)(t.tm_mon + 1)),  toBcd((uint8_t)(t.tm_year % 100)),
    };
    return writeRegisters(ADDR_DS3231, 0x00, r, 7);
  }
```

- [ ] **Step 5: Simplify `boardRevision()`**

Replace `RefClock::boardRevision()` in `RefCounter/RefClock.cpp` with:

```c
uint8_t RefClock::boardRevision() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  if (info.model != CHIP_ESP32) {
    return 30; // an S3 here means V3
  }
  // An ESP32 with a PCF8563 on the bus is a V2.0. Finding nothing means the
  // chip did not answer, which is worth reporting as unknown rather than
  // guessing a revision from it.
  return _rtc.kind() == RefRtc::PCF8563 ? 20 : 0;
}
```

Update the declaration's comment in `RefCounter/RefClock.h`:

```c
  // Battery terminal voltage, and 20 / 30 for the board revision -- or 0 if
  // no clock answered, so the revision could not be told.
  float batteryVolts();
  uint8_t boardRevision();
```

- [ ] **Step 6: Drop the DS3231 label from the About screen**

In `RefCounter/RefMenu.cpp`, delete this line from the `switch` in `showAbout`:

```c
  case RefRtc::DS3231:   rtcName = "DS3231"; break;
```

leaving:

```c
  const char *rtcName = "none";
  switch (refClock.rtc().kind()) {
  case RefRtc::PCF8563:  rtcName = "PCF8563"; break;
  case RefRtc::INTERNAL: rtcName = "internal"; break;
  default: break;
  }
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `./tests/run.sh`
Expected: all tests pass; `board_test` prints `board: V2.0 pin map and RTC kinds ok`.

Run: `pio run -e watchy_v2 -e watchy_v3`
Expected: two `SUCCESS` lines. Watch for warnings about an unhandled enumeration value in a `switch` — there should be none; the `read()` and `set()` switches keep their `default` labels.

- [ ] **Step 8: Confirm nothing still references the chip**

Run:

```bash
grep -rn "DS3231" RefCounter tests platformio.ini
```

Expected: no output. (`README.md` still mentions it at this point; Task 3 handles the docs.)

- [ ] **Step 9: Commit**

```bash
git add RefCounter/RefRtc.h RefCounter/RefRtc.cpp RefCounter/RefClock.h RefCounter/RefClock.cpp RefCounter/RefMenu.cpp tests/board_test.cpp
git commit -m "rtc: remove the DS3231 driver with V1.0

It was fitted only to V1.0. V1.5 and V2.0 both ship the PCF8563, which
is why boardRevision() needed a build-time #if to tell those two apart;
with V1.5 gone the probe answers on its own."
```

---

## Task 3: Bring the docs in line

The README, `settings.h` and the test runner still describe four revisions, two RTC chips and four envs. This task is prose only — no behaviour changes — but it is where a user who owns a v1 Watchy finds out where they stand.

**Files:**
- Modify: `README.md` (sections listed below)
- Modify: `RefCounter/settings.h:116-119`

**Interfaces:**
- Consumes: the final state of `platformio.ini`, `board.h` and `RefRtc` from Tasks 1 and 2. Every number quoted below matches those files.
- Produces: nothing consumed by later tasks — this is the last one.

- [ ] **Step 1: Fix the drift comment in `settings.h`**

Replace the first sentence of the `NTP_RESYNC_HOURS` comment block:

```c
// The RTC drifts a little every day - roughly a minute a month on the PCF8563
// fitted to V1.5 and V2, far less on the temperature-compensated DS3231 in
// V1.0. Re-syncing over WiFi keeps it well under a second, but a sync blocks
```

with:

```c
// The RTC drifts a little every day - roughly a minute a month on the PCF8563
// fitted to V2, and on V3 rather more, since the S3 keeps time on a bare
// 32kHz crystal with no temperature compensation at all. Re-syncing over WiFi
// keeps it well under a second, but a sync blocks
```

- [ ] **Step 2: Fix the Timekeeping section of `README.md`**

Replace:

```
The time comes from the watch's RTC chip, which `RefRtc` probes for at boot —
DS3231 at 0x68 on V1.0, PCF8563 at 0x51 on V1.5 and V2, and on V3 the
ESP32-S3's own 32kHz-backed clock. It keeps running through low power mode and
```

with:

```
The time comes from the watch's RTC chip, which `RefRtc` probes for at boot —
PCF8563 at 0x51 on V2, and on V3 the ESP32-S3's own 32kHz-backed clock. It
keeps running through low power mode and
```

And replace the drift sentence at the end of that section:

```
`NTP_RESYNC_HOURS` to 0 to only ever sync by hand. A PCF8563 drifts roughly a
minute a month, so even a daily sync keeps that under a second. The DS3231 in
V1.0 is temperature compensated and drifts far less.
```

with:

```
`NTP_RESYNC_HOURS` to 0 to only ever sync by hand. A PCF8563 drifts roughly a
minute a month, so even a daily sync keeps that under a second.
```

- [ ] **Step 3: Fix the Repo structure section of `README.md`**

Three lines in the tree block:

- `├── platformio.ini     build config; one env per Watchy revision` → `├── platformio.ini     build config; one env per supported revision (V2, V3)`
- `    ├── RefRtc.h/.cpp      DS3231 / PCF8563 / ESP32-S3 clock, over I2C` → `    ├── RefRtc.h/.cpp      PCF8563 / ESP32-S3 clock, over I2C`

The `board.h` line — `the pin map, per revision, and button polarity` — stays accurate and is left alone.

Then, in the *Dependencies* prose below the table, replace:

```
Everything else is either this project's own or comes with the ESP32 Arduino
core. Notably there is **no RTC library**: the DS3231 and PCF8563 are a handful
of BCD registers each, and talking to them directly avoids two dependencies
that both caused trouble — one whose `master` no longer compiles against a
current core, and one that `#define`s `i2cRead` and `i2cWrite` as bare macros.
```

with:

```
Everything else is either this project's own or comes with the ESP32 Arduino
core. Notably there is **no RTC library**: the PCF8563 is a handful of BCD
registers, and talking to it directly avoids a dependency that caused trouble
— one whose `master` no longer compiles against a current core.
```

- [ ] **Step 4: Fix the build instructions in `README.md`**

In *Option A — PlatformIO*, replace:

```
on the first build. Plug the watch in, then build and flash. Pick the env for your revision —
`watchy_v2` (the default), `watchy_v15`, `watchy_v10`, or `watchy_v3`:
```

with:

```
on the first build. Plug the watch in, then build and flash. Pick the env for
your revision — `watchy_v2` (the default) or `watchy_v3`:
```

In *Option B — Arduino IDE*, replace step 3's first paragraph:

```
3. **Board target.** *Tools → Board → ESP32 Arduino* → **ESP32 Dev Module**
   for V1.0/V1.5/V2, or **ESP32S3 Dev Module** for V3. The pin map is this
   project's own, so the board only has to be the right chip.

   Then set the revision, because the up button and the battery tap moved
   between them. The IDE has no build-flag field, so uncomment the matching
   line near the top of [`RefCounter/board.h`](RefCounter/board.h) — it has to
   go in that header, which every source file includes, not in the `.ino`,
   which would only set it for itself:

   ```c
   // #define ARDUINO_WATCHY_V20   // or _V15, or _V10
   ```
```

with:

```
3. **Board target.** *Tools → Board → ESP32 Arduino* → **ESP32 Dev Module**
   for V2, or **ESP32S3 Dev Module** for V3. The pin map is this project's
   own, so the board only has to be the right chip.

   Then set the revision. The IDE has no build-flag field, so uncomment this
   line near the top of [`RefCounter/board.h`](RefCounter/board.h) — it has to
   go in that header, which every source file includes, not in the `.ino`,
   which would only set it for itself:

   ```c
   // #define ARDUINO_WATCHY_V20
   ```
```

In *If upload fails*, replace:

```
On a **V3**, hold the **top-right (UP)** button while plugging in to force the
bootloader — on that revision UP is GPIO 0, the ESP32-S3 strapping pin. The
ESP32 revisions have no button on GPIO 0 and rely on the USB serial chip's
auto-reset instead, so there is no button to hold; if auto-reset is not
working, it is a cable or port problem rather than a timing one.
```

with:

```
On a **V3**, hold the **top-right (UP)** button while plugging in to force the
bootloader — on that revision UP is GPIO 0, the ESP32-S3 strapping pin. V2 has
no button on GPIO 0 and relies on the USB serial chip's auto-reset instead, so
there is no button to hold; if auto-reset is not working, it is a cable or
port problem rather than a timing one.
```

- [ ] **Step 5: Add the supported-revisions note to `README.md`**

Directly above the `### Option A — PlatformIO` heading, add:

```markdown
This firmware supports **Watchy V2.0** (ESP32) and **V3** (ESP32-S3). V1.0 and
V1.5 were dropped: their up button and battery tap sit on different pins, and
V1.0 carries a different RTC chip entirely. `board.h` stops the build if their
revision flags are set, rather than flashing a pin map that would misread the
battery and never see the up button.
```

- [ ] **Step 6: Fix the Testing and Known limitations sections of `README.md`**

In *Testing*, leave the existing paragraph about `RefZone.cpp` and friends
alone — it names no revision — and append a paragraph at the end of the
section, immediately before the `## Known limitations` heading:

```markdown
`tests/board_test.cpp` adds a second kind of check: it asserts the V2.0 pin
numbers and the surviving `RefRtc::Kind` values, and `tests/run.sh` then
compiles `board.h` twice more with the retired V1.0 and V1.5 flags to confirm
each one is rejected. Those two are the only facts here a wrong edit could
break silently — everything else is caught by the compiler.
```

In *Known limitations*, replace:

```
- **None of this has been run on a watch.** It builds clean for all four
  revisions and the time zone, digit-layout and sport-preset logic are tested
```

with:

```
- **None of this has been run on a watch.** It builds clean for both supported
  revisions and the time zone, digit-layout and sport-preset logic are tested
```

The menu table's About row — `| About | Version, board revision, battery, time, uptime, last sync, RTC type |` — names no revision and stays as it is.

- [ ] **Step 7: Verify no stale references survive**

Run:

```bash
grep -rn "V1\.0\|V1\.5\|watchy_v10\|watchy_v15\|DS3231\|ARDUINO_WATCHY_V1[05]" README.md RefCounter tests platformio.ini
```

Expected: the only hits are the deliberate ones — `board.h`'s guard comment and `#error`, `tests/board_test.cpp`'s comments and assert messages, `tests/run.sh`'s guard block, and the README's supported-revisions note plus the `board_test` paragraph. Anything else is a leftover; fix it before moving on.

- [ ] **Step 8: Run the full verification**

Run: `./tests/run.sh`
Expected: every test passes, exit status 0.

Run: `pio run -e watchy_v2 -e watchy_v3`
Expected: two `SUCCESS` lines.

Run: `git status --short`
Expected: the `board-files/` modifications are still there, untouched. Confirm nothing under `board-files/` was staged by any commit in this plan: `git log --stat -3 -- board-files` should show no commits from this work.

- [ ] **Step 9: Commit**

```bash
git add README.md RefCounter/settings.h
git commit -m "docs: describe the two supported revisions

The README and settings.h still walked a reader through building for
V1.0 and V1.5 and quoted the DS3231's drift figures. Says what the
firmware now supports, and why the retired flags stop the build."
```

---

## Verification

After all three tasks, from a clean checkout of the branch:

```bash
./tests/run.sh && pio run -e watchy_v2 -e watchy_v3
```

Both must succeed, and `run.sh`'s last block must print:

```
=== board_guard
  -D ARDUINO_WATCHY_V10 rejected
  -D ARDUINO_WATCHY_V15 rejected
```

Then confirm the retired envs are gone rather than merely unused:

```bash
pio run -e watchy_v10
```

Expected: `Unknown environment names 'watchy_v10'`, with `watchy_v2, watchy_v3` listed as the valid ones.
