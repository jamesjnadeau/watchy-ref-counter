# Drop Watchy V1.0, V1.5 and V3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the supported boards from five to two — Watchy V2.0 (`watchy_v2`) and this repo's own ESP32-C6 board (`watchy_c6`) — removing V1.0, V1.5 and V3 from the build, the pin map, the RTC layer and the docs.

**Architecture:** Mostly deletion. `board.h` goes from four pin maps to two, `RefRtc::Kind` from four values to two (`NONE`, `PCF8563`), and `RefClock` loses its `esp_chip_info` probe. The one non-deletion is that three `ARDUINO_ESP32S3_DEV` conditionals scattered through the firmware have to be resolved by hand rather than deleted: two of them guard blocks the C6 needs, and one of those — `RefPanel::begin()`'s explicit `SPI.begin()` — is a live C6 defect the removal exposes. A `#error` guard replaces all three retired flags so a stale `-D`, or simply picking an S3 board, fails loudly instead of silently building a pin map for other hardware.

**Tech Stack:** C++17, Arduino/ESP32 core, PlatformIO (`espressif32` for `watchy_v2`, pinned pioarduino fork for `watchy_c6`), GxEPD2. Host tests are plain `g++` binaries driven by `tests/run.sh`.

**Spec:** [`docs/superpowers/specs/2026-08-19-drop-watchy-v1-v3.md`](../specs/2026-08-19-drop-watchy-v1-v3.md)

## Global Constraints

- **Supported boards after this work:** Watchy V2.0 (`watchy_v2`, the default env, ESP32, PCF8563 @ 0x51) and this repo's C6 board (`watchy_c6`, ESP32-C6-MINI-1). Nothing else.
- **Every task ends with both envs building:** `pio run -e watchy_v2 -e watchy_c6`. The first `watchy_c6` build pulls the pioarduino platform and takes several minutes; later ones are quick.
- **Every task ends with the host suite passing:** `./tests/run.sh`.
- **Guard message, verbatim, in `board.h`:** `Watchy V1.0, V1.5 and V3 are no longer supported; build watchy_v2 or watchy_c6`
- **V2.0 pin numbers are facts, not choices,** and do not change in this work: `PIN_BTN_UP 35`, `PIN_BATT_ADC 34`, `PIN_BTN_MENU 26`, `PIN_BTN_BACK 25`, `PIN_BTN_DOWN 4`, `PIN_I2C_SDA 21`, `PIN_I2C_SCL 22`, `PIN_SPI_SCK 18`, `PIN_SPI_MOSI 23`, `PIN_DISPLAY_CS 5`, `PIN_DISPLAY_RST 9`, `PIN_DISPLAY_DC 10`, `PIN_DISPLAY_BUSY 19`, `PIN_VIB_MOTOR 13`, `BTN_PRESSED_LEVEL HIGH`.
- **C6 pin numbers likewise,** and they come from `board-files/elec/src/watchy.ato`: `PIN_I2C_SDA 7`, `PIN_I2C_SCL 6`, `PIN_SPI_SCK 15`, `PIN_SPI_MISO 23`, `PIN_SPI_MOSI 14`, `PIN_SPI_SS 8`, `PIN_BTN_MENU 0`, `PIN_BTN_BACK 1`, `PIN_BTN_UP 2`, `PIN_BTN_DOWN 3`, `PIN_DISPLAY_CS 8`, `PIN_DISPLAY_DC 18`, `PIN_DISPLAY_RST 19`, `PIN_DISPLAY_BUSY 20`, `PIN_VIB_MOTOR 21`, `PIN_BATT_ADC 5`, `BTN_PRESSED_LEVEL LOW`. **Do not edit the C6 block** except to add `BOARD_NAME` in Task 4.
- **The RV-3028-C7 driver is out of scope.** The C6 board's RTC has never been supported and still will not be after this work. Do not add it; do not "fix" `RefRtc` to probe 0x52. It has its own spec coming.
- **The working tree has large unrelated in-progress changes** under `board-files/`, plus modifications to `README.md` and `RefCounter/`. Do **not** run `git add -A` or `git commit -a`. Every commit step below lists exact paths; use those, and check `git status --short` afterwards to confirm `board-files/` is still dirty and unstaged.
- **Comment style:** this codebase explains *why*, in full sentences, in the file the reader will be in. Match it. Never leave a bare `// removed V3` marker.

---

## File Structure

**Modified:**

| File | Responsibility after this work |
| --- | --- |
| `platformio.ini` | Two envs: `watchy_v2` (default) and `watchy_c6` |
| `RefCounter/board.h` | Two pin maps (C6, V2.0), the `#error` guard for retired flags, and `BOARD_NAME` |
| `RefCounter/RefRtc.h` | `Kind` = `{NONE, PCF8563}` |
| `RefCounter/RefRtc.cpp` | PCF8563 probe/read/write only |
| `RefCounter/RefPanel.cpp` | `SPI.begin()` under the C6 flag — the fix |
| `RefCounter/RefCounter.ino` | Deep-sleep GPIO prep under the C6 flag |
| `RefCounter/RefClock.h` / `.cpp` | No `boardRevision()`, no `esp_chip_info` |
| `RefCounter/RefMenu.cpp` | About screen prints `BOARD_NAME`; no DS3231 or internal-clock labels |
| `RefCounter/settings.h` | Drift comment for the two surviving boards |
| `README.md` | Two supported boards throughout |
| `tests/run.sh` | Runs both board tests, plus a negative-compile check per retired flag |

**Created:**

| File | Responsibility |
| --- | --- |
| `tests/stub/Arduino.h` | Minimal host stub so `board.h` and `RefRtc.h` compile off-target |
| `tests/board_test.cpp` | Asserts the V2.0 pin map, its `BOARD_NAME`, and the surviving `RefRtc::Kind` values |
| `tests/board_c6_test.cpp` | Asserts the C6 pin map and its `BOARD_NAME` — `board.h` notes that nothing asserts this half; this is that assertion |

---

## Task 1: Retire the V1.0 and V1.5 build envs and pin map

Removes the two v1 envs and the per-revision `#if` in `board.h`, and makes the retired v1 flags a build error. Ships the host-side scaffolding the later tasks assert against.

**Files:**
- Modify: `platformio.ini`
- Modify: `RefCounter/board.h` (the revision-selection comment block and the trailing ESP32 branch)
- Create: `tests/stub/Arduino.h`
- Create: `tests/board_test.cpp`
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `board.h` exports the same macro names it does today. No macro is renamed or removed in this task; only the V1.0/V1.5 *values* disappear.
- Produces: `tests/stub/Arduino.h` defines `LOW` (0x0) and `HIGH` (0x1) and includes `<cstdint>`. Tasks 2 and 4 use it via `RefRtc.h` and `board.h`.
- Produces: `tests/run.sh` gains a `board_test` entry with an empty source list, and a `board_guard` block after the main loop that later tasks extend.

- [ ] **Step 1: Create the host stub for `Arduino.h`**

`board.h` includes `<Arduino.h>` and takes exactly two things from it: the `LOW` and `HIGH` pin levels used by `BTN_PRESSED_LEVEL`. Its other core references (`ESP_EXT1_WAKEUP_ANY_HIGH`, `GPIO_INTR_HIGH_LEVEL`) sit inside `#define` bodies that nothing on the host expands.

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

- [ ] **Step 2: Write the V2.0 pin-map test**

This one passes before the change as well as after — the V2.0 numbers are what a flagless build already selects. It is here so that collapsing the `#if` in Step 6 is provably a no-op for V2.0, and so a later edit that moves a pin fails a test instead of a watch.

Create `tests/board_test.cpp`:

```cpp
// The pin map is a fact about the hardware, not a preference, so it gets a
// test that fails loudly if a define moves. These are the V2.0 numbers; the
// C6 board's are in board_c6_test.cpp.
//
// V1.0, V1.5 and V3 are no longer supported and board.h #errors on their
// flags. That half is checked by tests/run.sh, because a compile that has to
// fail cannot live in a file that has to compile.

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
              "the ESP32 board reads high when a button is pressed");

int main() {
  printf("board: V2.0 pin map ok\n");
  return 0;
}
```

- [ ] **Step 3: Wire the test and the guard check into the runner**

Three edits to `tests/run.sh`.

First, add `board_test` to the table and the loop — it needs no source files of its own, so its entry is an empty string:

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
# board.h has to refuse the retired revision flags outright. A leftover -D, or
# an Arduino IDE user's stale #define, must stop the build rather than quietly
# selecting the V2.0 map -- on V1 hardware the battery tap and the up button
# are on other pins entirely. This is a compile that has to fail, which is why
# it lives here rather than in a .cpp.
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

Third, update the comment block at the top of the file so its file list is not stale. Replace:

```bash
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp
# and RefMenuItems.cpp are the files with logic worth testing off the watch: no
```

with:

```bash
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp
# and RefMenuItems.cpp are the files with logic worth testing off the watch,
# and board.h's pin maps are checked here too. Between them: no
```

- [ ] **Step 4: Run the tests and watch the guard check fail**

Run: `./tests/run.sh`

Expected: `board_test` prints `board: V2.0 pin map ok` and passes. Then `board_guard` fails with `FAIL: board.h accepted -D ARDUINO_WATCHY_V10` and the script exits non-zero. That is the red test — `board.h` has no guard yet.

- [ ] **Step 5: Add the guard and drop the v1 flags from `board.h`**

In `RefCounter/board.h`, replace the revision-selection prose — from `// The revision is selected by the build:` down to the `#endif` that closes the `#warning` block — with this, keeping the existing `// ----` rule and `#include <Arduino.h>` line where they already sit:

```c
// The revision is selected by the build: ARDUINO_WATCHY_V20 for the Watchy
// V2.0 (ESP32), ARDUINO_ESP32C6_DEV for this repo's own ESP32-C6-MINI-1
// board. See platformio.ini.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// PlatformIO sets the revision from platformio.ini. The Arduino IDE has no
// build-flag field, so IDE users on a V2.0 uncomment the line below -- it has
// to live in this header, which every source file includes, rather than in
// the .ino, which only sets it for itself.
//
// #define ARDUINO_WATCHY_V20
//
// The C6 board needs nothing: selecting an ESP32-C6 board defines
// ARDUINO_ESP32C6_DEV.

// V1.0 and V1.5 are no longer supported. Their up button and battery tap sat
// on other pins than V2.0's -- 32/33 on V1.0 and 32/35 on V1.5, against
// 35/34 here -- so quietly building the V2.0 map for one of them would read
// the battery off the wrong pin and never see the up button at all. A stale
// build flag or an old #define left in this file therefore stops the build.
#if defined(ARDUINO_WATCHY_V10) || defined(ARDUINO_WATCHY_V15)
#error "Watchy V1.0, V1.5 and V3 are no longer supported; build watchy_v2 or watchy_c6"
#endif

#if !defined(ARDUINO_ESP32S3_DEV) && !defined(ARDUINO_ESP32C6_DEV) &&          \
    !defined(ARDUINO_WATCHY_V20)
#warning "No Watchy revision defined; assuming V2.0"
#define ARDUINO_WATCHY_V20
#endif
```

The message names V3 already even though the S3 flag is not caught until Task 3; that keeps one message string in the file rather than two, and Task 3 only widens the `#if`.

- [ ] **Step 6: Collapse the ESP32 pin map to V2.0**

In the same file, replace the final `#else` branch — the one headed `// --- V1.0 / V1.5 / V2.0 (ESP32) ---` — down to just before `// Buttons pull their pin high when pressed.`, with:

```c
#else
// --- Watchy V2.0 (ESP32) ---------------------------------------------------
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

Everything after that — the button-polarity comment and its three defines, the divider comment, `#define BATT_DIVIDER 2.0f`, and the closing `#endif` — stays exactly as it is. The `ARDUINO_ESP32C6_DEV` and `ARDUINO_ESP32S3_DEV` branches above are untouched in this task.

- [ ] **Step 7: Run the tests and watch them pass**

Run: `./tests/run.sh`

Expected: everything passes, ending with

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

Then fix the ESP32 section header just above them, which now describes one env rather than three. Replace:

```ini
; --- ESP32 revisions -------------------------------------------------------
; Generic ESP32 boards; the pin map is this project's own, in board.h. The
; revision define is what selects between them.
```

with:

```ini
; --- Watchy V2.0 (ESP32) ---------------------------------------------------
; A generic ESP32 board; the pin map is this project's own, in board.h. The
; revision define is what selects it over the C6 map.
```

Leave the file's opening comment block and the `watchy_v3` env alone — Task 3 rewrites both.

- [ ] **Step 9: Verify the builds**

Run: `pio run -e watchy_v2 -e watchy_c6`
Expected: two `SUCCESS` lines.

Run: `pio run -e watchy_v10`
Expected: failure — `Unknown environment names 'watchy_v10'`.

Clear the stale build directories:

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

The DS3231 was fitted only to V1.0. With that revision gone, the chip has no board to sit on.

**Files:**
- Modify: `RefCounter/RefRtc.h`
- Modify: `RefCounter/RefRtc.cpp`
- Modify: `RefCounter/RefClock.cpp` (`boardRevision()`)
- Modify: `RefCounter/RefMenu.cpp` (`showAbout`)
- Modify: `tests/board_test.cpp`

**Interfaces:**
- Consumes: `tests/stub/Arduino.h` and `tests/board_test.cpp` from Task 1; the runner entry for `board_test` already exists.
- Produces: `RefRtc::Kind` is `enum Kind : uint8_t { NONE, PCF8563, INTERNAL }` — values 0, 1, 2. Task 3 removes `INTERNAL` and leaves `{NONE, PCF8563}`.
- Produces: `RefRtc`'s public surface is otherwise unchanged: `void begin()`, `Kind kind() const`, `bool read(struct tm &out)`, `bool set(const struct tm &t)`, `time_t epoch()`.
- Produces: `uint8_t RefClock::boardRevision()` still exists and still returns 30 for a non-ESP32 SoC; Task 4 deletes it.

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
```

and update `main()`:

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

because `DS3231` still occupies value 1.

- [ ] **Step 3: Drop DS3231 from `RefRtc.h`**

Replace the class comment and enum — from `// The watch's real time clock.` through the `Kind` line — with:

```c
// The watch's real time clock.
//
// Which one that is depends on the board, so this probes for what is actually
// on the bus:
//   V2.0  PCF8563  at 0x51
//   C6    nothing this firmware can read yet; the board's RV-3028-C7 has no
//         driver here, so it reports NONE
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

Delete the `ADDR_DS3231` constant, leaving `const uint8_t ADDR_PCF8563 = 0x51;`.

Replace the non-S3 half of `begin()` — the `if (present(ADDR_DS3231)) { ... } else if (present(ADDR_PCF8563)) { ... } else { ... }` chain — with:

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

Leave the `#ifdef ARDUINO_ESP32S3_DEV` / `#else` structure around it alone — Task 3 removes it.

Delete the whole `readDS3231` function together with the two-line `// DS3231 registers 0x00..0x06: ...` comment above it, the `case DS3231:` from `read()`, and the `case DS3231: { ... }` block from `set()`.

- [ ] **Step 5: Drop the DS3231 arms from `boardRevision()` and the About screen**

In `RefCounter/RefClock.cpp`, replace the `switch` inside `boardRevision()` and the comment above it with:

```c
  // V2.0 ships a PCF8563. Finding nothing means the chip did not answer,
  // which is worth reporting as unknown rather than guessing a revision.
  return _rtc.kind() == RefRtc::PCF8563 ? 20 : 0;
```

so the function reads:

```c
uint8_t RefClock::boardRevision() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  if (info.model != CHIP_ESP32) {
    return 30; // an S3 here means V3
  }
  // V2.0 ships a PCF8563. Finding nothing means the chip did not answer,
  // which is worth reporting as unknown rather than guessing a revision.
  return _rtc.kind() == RefRtc::PCF8563 ? 20 : 0;
}
```

(Task 4 deletes this function outright; it just has to compile and stay honest in between.)

In `RefCounter/RefMenu.cpp`, delete this line from the `switch` in `showAbout`:

```c
  case RefRtc::DS3231:   rtcName = "DS3231"; break;
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `./tests/run.sh`
Expected: all pass; `board_test` prints `board: V2.0 pin map and RTC kinds ok`.

Run: `pio run -e watchy_v2 -e watchy_c6`
Expected: two `SUCCESS` lines, with no unhandled-enumeration warnings — the `read()` and `set()` switches keep their `default` labels.

- [ ] **Step 7: Confirm nothing still references the chip**

Run: `grep -rn "DS3231" RefCounter tests platformio.ini`
Expected: no output. (`README.md` still mentions it; Task 5 handles the docs.)

- [ ] **Step 8: Commit**

```bash
git add RefCounter/RefRtc.h RefCounter/RefRtc.cpp RefCounter/RefClock.cpp RefCounter/RefMenu.cpp tests/board_test.cpp
git commit -m "rtc: remove the DS3231 driver with V1.0

It was fitted only to V1.0. V1.5 and V2.0 both ship the PCF8563, which
is why boardRevision() needed a build-time #if to tell those two apart;
with V1.5 gone the probe answers on its own."
```

---

## Task 3: Retire V3, and give the C6 the blocks that were hiding behind it

The largest task, and the only one that is not pure deletion. Three `ARDUINO_ESP32S3_DEV` conditionals guard behaviour, and each needs a decision rather than a delete:

| Site | What it guards | What it becomes | Why |
| --- | --- | --- | --- |
| `RefPanel.cpp` | explicit `SPI.begin(SCK, MISO, MOSI, SS)` | `#ifdef ARDUINO_ESP32C6_DEV` | **A fix.** The C6's SPI pins are 15/23/14/8, not the default bus, so it needs this call and has never had it. Deleting the block would leave the C6 panel unable to talk. |
| `RefCounter.ino` | `rtc_gpio_*` prep before deep sleep | `#ifdef ARDUINO_ESP32C6_DEV` | The condition is already `S3 || C6`; dropping the S3 arm leaves the C6 arm, which is exactly today's behaviour for both surviving boards. |
| `RefRtc.cpp` | `_kind = INTERNAL` for the S3's own clock | deleted | The C6 has an external RTC. Nothing else sets `INTERNAL`, so the enum value goes too. |

**Files:**
- Modify: `platformio.ini`
- Modify: `RefCounter/board.h` (guard `#if`, the S3 pin-map block, header comment)
- Modify: `RefCounter/RefPanel.cpp`
- Modify: `RefCounter/RefCounter.ino`
- Modify: `RefCounter/RefRtc.h`, `RefCounter/RefRtc.cpp`
- Modify: `RefCounter/RefMenu.cpp`
- Modify: `tests/run.sh`
- Create: `tests/board_c6_test.cpp`

**Interfaces:**
- Consumes: the guard `#if` and `board_guard` loop from Task 1; `RefRtc::Kind` from Task 2.
- Produces: `RefRtc::Kind` is `enum Kind : uint8_t { NONE, PCF8563 };` — values 0 and 1. `INTERNAL` no longer exists; any `case RefRtc::INTERNAL` anywhere is a compile error, which is the point.
- Produces: `board.h` has exactly two pin-map branches, `#ifdef ARDUINO_ESP32C6_DEV` and `#else`.

- [ ] **Step 1: Write the failing tests**

Create `tests/board_c6_test.cpp`. `board.h` says of the C6 block that "board-files/checks/netlist_check.py asserts the netlist half, and nothing asserts this half" — this file is that assertion:

```cpp
// The C6 board's pin map, asserted. Every number here comes from
// board-files/elec/src/watchy.ato by way of board.h; board.h's own comment
// notes that the netlist half is checked by board-files/checks and this half
// was checked by nothing. Now it is checked by this.

#define ARDUINO_ESP32C6_DEV // as platformio.ini's watchy_c6 env does

#include <stdio.h>

#include "board.h"

static_assert(PIN_I2C_SDA == 7 && PIN_I2C_SCL == 6, "C6 I2C pins");
static_assert(PIN_SPI_SCK == 15 && PIN_SPI_MISO == 23 && PIN_SPI_MOSI == 14 &&
                  PIN_SPI_SS == 8,
              "C6 SPI pins; MISO is a spare, the panel is write-only");
static_assert(PIN_BTN_MENU == 0 && PIN_BTN_BACK == 1 && PIN_BTN_UP == 2 &&
                  PIN_BTN_DOWN == 3,
              "C6 buttons are IO0-IO3, all LP GPIOs so one ext1 mask covers "
              "them");
static_assert(PIN_DISPLAY_CS == 8 && PIN_DISPLAY_DC == 18 &&
                  PIN_DISPLAY_RST == 19 && PIN_DISPLAY_BUSY == 20,
              "C6 panel control pins");
static_assert(PIN_VIB_MOTOR == 21, "C6 vibration motor");
static_assert(PIN_BATT_ADC == 5, "C6 battery tap");
static_assert(BTN_PRESSED_LEVEL == LOW,
              "C6 buttons pull their pin low when pressed");

int main() {
  printf("board: C6 pin map ok\n");
  return 0;
}
```

Add it to `tests/run.sh` — a `[board_c6_test]=""` entry in the table and `board_c6_test` at the end of the `for` list.

Then extend the `board_guard` loop in `tests/run.sh` to cover the S3 flag:

```bash
for flag in ARDUINO_WATCHY_V10 ARDUINO_WATCHY_V15 ARDUINO_ESP32S3_DEV; do
```

Nothing is added to `tests/board_test.cpp` in this task. `INTERNAL`'s removal
needs no assertion: it is referenced by name in `RefMenu.cpp` and `RefRtc.cpp`,
so deleting the enumerator either breaks the build or the references are gone.
The compiler is the test.

- [ ] **Step 2: Run the tests to verify the guard fails**

Run: `./tests/run.sh`

Expected: `board_c6_test` compiles and passes — the C6 block is already correct, so this file pins it rather than fixing it. Then `board_guard` fails with `FAIL: board.h accepted -D ARDUINO_ESP32S3_DEV` and the script exits non-zero. That is the red test.

- [ ] **Step 3: Widen the guard and delete the S3 pin map**

In `RefCounter/board.h`:

Widen the guard `#if` and its comment:

```c
// V1.0, V1.5 and V3 are no longer supported. The two V1 revisions put the up
// button and the battery tap on other pins than V2.0's -- 32/33 on V1.0 and
// 32/35 on V1.5, against 35/34 here -- and V3 is an ESP32-S3, which has no
// GPIO 26 or 35 to build the V2.0 map on at all. Quietly falling through to
// that map would read the battery off the wrong pin and never see the up
// button, so a stale build flag, an old #define left in this file, or simply
// picking an S3 board stops the build instead.
#if defined(ARDUINO_WATCHY_V10) || defined(ARDUINO_WATCHY_V15) ||              \
    defined(ARDUINO_ESP32S3_DEV)
#error "Watchy V1.0, V1.5 and V3 are no longer supported; build watchy_v2 or watchy_c6"
#endif
```

Drop the now-unreachable S3 term from the default-revision `#if`:

```c
#if !defined(ARDUINO_ESP32C6_DEV) && !defined(ARDUINO_WATCHY_V20)
#warning "No Watchy revision defined; assuming V2.0"
#define ARDUINO_WATCHY_V20
#endif
```

Delete the entire `#elif defined(ARDUINO_ESP32S3_DEV)` branch — from that line through the `#define BATT_DIVIDER 2.0f` that ends it — so the C6 block's `#ifdef` runs straight into the `#else` holding the V2.0 map.

Finally, check the file's opening comment for any surviving mention of V3 or the S3 and fix it; after Task 1 it should already read `ARDUINO_WATCHY_V20 ... ARDUINO_ESP32C6_DEV`, but the C6 block's own comment `// 1M/1M divider, as on the S3 board -- 2:1, and 2.1uA of standing drain.` refers to hardware that no longer appears anywhere in this repo's firmware. Replace it with:

```c
// 1M/1M divider: 2:1, and 2.1uA of standing drain against V2.0's 21uA.
```

- [ ] **Step 4: Give the C6 its `SPI.begin()`**

In `RefCounter/RefPanel.cpp`, replace the guarded block in `begin()`:

```c
#ifdef ARDUINO_ESP32C6_DEV
  // The C6 has no default pin assignment for this bus, so it has to be
  // spelled out before the panel is touched. This block used to be ESP32-S3
  // only, which left the C6 -- whose SPI pins are just as non-default --
  // talking to the panel over whatever the core picked.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SS);
#endif
```

The V2.0 build has no `PIN_SPI_MISO` or `PIN_SPI_SS` defined at all, which is why this stays inside a `#ifdef` rather than becoming unconditional.

- [ ] **Step 5: Narrow the deep-sleep block to the C6**

In `RefCounter/RefCounter.ino`, change:

```c
#if defined(ARDUINO_ESP32S3_DEV) || defined(ARDUINO_ESP32C6_DEV)
```

to:

```c
#ifdef ARDUINO_ESP32C6_DEV
```

Nothing inside either arm changes.

- [ ] **Step 6: Remove the internal-clock kind from `RefRtc`**

In `RefCounter/RefRtc.h`, drop `INTERNAL` from the enum:

```c
  enum Kind : uint8_t { NONE, PCF8563 };
```

In `RefCounter/RefRtc.cpp`, delete the `#ifdef ARDUINO_ESP32S3_DEV` / `#else` / `#endif` scaffolding around the body of `begin()`, leaving the probe unconditional:

```c
void RefRtc::begin() {
  if (present(ADDR_PCF8563)) {
    _kind = PCF8563;
    // Control 1 and 2 cleared: run normally, no alarm or timer interrupts.
    const uint8_t control[2] = {0x00, 0x00};
    writeRegisters(ADDR_PCF8563, 0x00, control, 2);
  } else {
    _kind = NONE;
  }
}
```

Delete the `case INTERNAL:` arm from `read()` — the block that calls `time(nullptr)` and `localtime_r` — and the `case INTERNAL: return true;` arm from `set()`. `set()` keeps its `settimeofday()` call at the top; the comment above it says it is there so V3 "has somewhere to store it at all", which is no longer the reason. Replace that comment with:

```c
  // Keep the SoC's own clock in step whatever else is fitted, so time() and
  // anything built on it agree with the chip.
```

In `RefCounter/RefMenu.cpp`, delete the now-dead arm from `showAbout`'s switch:

```c
  case RefRtc::INTERNAL: rtcName = "internal"; break;
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `./tests/run.sh`

Expected: all pass, ending with

```
=== board_guard
  -D ARDUINO_WATCHY_V10 rejected
  -D ARDUINO_WATCHY_V15 rejected
  -D ARDUINO_ESP32S3_DEV rejected
```

- [ ] **Step 8: Remove the v3 env and rewrite `platformio.ini`'s header**

Delete:

```ini
; --- ESP32-S3 revision -----------------------------------------------------
[env:watchy_v3]
board = esp32-s3-devkitc-1
build_flags = -D ARDUINO_ESP32S3_DEV
```

Replace the file's opening comment block with:

```ini
; Watchy Ref Counter
;
; The sketch lives in RefCounter/ so the same folder works unchanged as an
; Arduino IDE sketch (RefCounter/RefCounter.ino) and as the PlatformIO source
; directory. Two boards are supported; pick the env that matches yours:
;
;   pio run -e watchy_v2 -t upload      (default; Watchy V2.0, ESP32)
;   pio run -e watchy_c6 -t upload      (this repo's own board)
;
; Watchy V1.0, V1.5 and V3 are not supported; board.h stops the build if their
; revision flags are set, including the ARDUINO_ESP32S3_DEV that picking an S3
; board would define for you.
```

In the `watchy_c6` env's comment block, fix the last line, which counts envs that no longer exist:

```ini
; The other four envs are untouched and still build on espressif32 @ 7.0.1.
```

becomes:

```ini
; watchy_v2 is untouched and still builds on the official espressif32.
```

- [ ] **Step 9: Verify the builds**

Run: `pio run -e watchy_v2 -e watchy_c6`
Expected: two `SUCCESS` lines.

Run: `pio run -e watchy_v3`
Expected: `Unknown environment names 'watchy_v3'`, with `watchy_v2, watchy_c6` listed as valid.

Then:

```bash
rm -rf .pio/build/watchy_v3
grep -rn "ESP32S3\|S3-MINI\|ARDUINO_ESP32S3_DEV" RefCounter tests platformio.ini
```

Expected from the grep: only the guard in `board.h` and the `board_guard` loop in `tests/run.sh`.

- [ ] **Step 10: Commit**

```bash
git add platformio.ini RefCounter/board.h RefCounter/RefPanel.cpp RefCounter/RefCounter.ino RefCounter/RefRtc.h RefCounter/RefRtc.cpp RefCounter/RefMenu.cpp tests/run.sh tests/board_c6_test.cpp
git commit -m "board: drop V3, and hand the C6 what was behind its flag

The S3 target has been superseded by this repo's own C6 board, and it
was guarding three blocks that needed deciding rather than deleting.
RefPanel's explicit SPI.begin() was the live one: the C6's SPI pins are
as non-default as the S3's, so it needed that call and never got it.
The deep-sleep GPIO prep narrows to the C6 arm it already had, and
RefRtc loses the internal-clock kind along with the SoC that used it.
The C6 pin map now has a host test, which board.h noted it lacked."
```

---

## Task 4: Replace the board revision number with a board name

`boardRevision()` returned 10/15/20/30 and the About screen printed it as `v2.0`. Of the two boards left, one has no revision number to print — `v6.0` would be nonsense — so the number becomes a name.

**Files:**
- Modify: `RefCounter/board.h` (add `BOARD_NAME` to both branches)
- Modify: `RefCounter/RefClock.h`, `RefCounter/RefClock.cpp`
- Modify: `RefCounter/RefMenu.cpp`
- Modify: `tests/board_test.cpp`, `tests/board_c6_test.cpp`

**Interfaces:**
- Consumes: the two-branch `board.h` from Task 3.
- Produces: `BOARD_NAME` — a string literal, `"V2.0"` or `"C6"` — defined once in each pin-map branch of `board.h`.
- Produces: `RefClock::boardRevision()` no longer exists. `RefClock.cpp` no longer includes `<esp_chip_info.h>`.

- [ ] **Step 1: Write the failing tests**

`BOARD_NAME` is a string, so this one is a runtime check rather than a `static_assert`. Add to `tests/board_test.cpp` — the include at the top with the others, the check inside `main()`:

```cpp
#include <string.h>
```

```cpp
int main() {
  if (strcmp(BOARD_NAME, "V2.0") != 0) {
    printf("FAIL: BOARD_NAME is \"%s\", want \"V2.0\"\n", BOARD_NAME);
    return 1;
  }
  printf("board: V2.0 pin map, name and RTC kinds ok\n");
  return 0;
}
```

And the matching pair in `tests/board_c6_test.cpp`:

```cpp
#include <string.h>
```

```cpp
int main() {
  if (strcmp(BOARD_NAME, "C6") != 0) {
    printf("FAIL: BOARD_NAME is \"%s\", want \"C6\"\n", BOARD_NAME);
    return 1;
  }
  printf("board: C6 pin map and name ok\n");
  return 0;
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `./tests/run.sh`
Expected: both board tests fail to compile — `error: 'BOARD_NAME' was not declared in this scope`.

- [ ] **Step 3: Define `BOARD_NAME` in both branches**

In `RefCounter/board.h`, add to the C6 branch, just under its `// --- watchy-ref-counter's own board ... ---` header:

```c
// What the About screen calls this board. The two supported boards do not
// share a numbering scheme -- one is a Watchy revision and one is this repo's
// own design -- so the screen prints a name rather than a number.
#define BOARD_NAME "C6"
```

and to the V2.0 branch, just under its `// --- Watchy V2.0 (ESP32) ---` header:

```c
#define BOARD_NAME "V2.0"
```

- [ ] **Step 4: Delete `boardRevision()`**

In `RefCounter/RefClock.h`, replace:

```c
  // Battery terminal voltage, and 10 / 15 / 20 / 30 for the board revision.
  float batteryVolts();
  uint8_t boardRevision();
```

with:

```c
  // Battery terminal voltage. Which board this is comes from board.h's
  // BOARD_NAME, not from here -- it is a build-time fact, not a measurement.
  float batteryVolts();
```

In `RefCounter/RefClock.cpp`, delete the whole `boardRevision()` function and the `#include <esp_chip_info.h>` line, which nothing else in the file uses.

- [ ] **Step 5: Print the name on the About screen**

In `RefCounter/RefMenu.cpp`, replace:

```c
  display.print("Board: v");
  display.println(refClock.boardRevision() / 10.0f, 1);
```

with:

```c
  display.print("Board: ");
  display.println(BOARD_NAME);
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `./tests/run.sh`
Expected: `board: V2.0 pin map, name and RTC kinds ok` and `board: C6 pin map and name ok`, everything green.

Run: `pio run -e watchy_v2 -e watchy_c6`
Expected: two `SUCCESS` lines, and no warning about an unused `esp_chip_info` include.

Run: `grep -rn "boardRevision\|esp_chip_info" RefCounter tests`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add RefCounter/board.h RefCounter/RefClock.h RefCounter/RefClock.cpp RefCounter/RefMenu.cpp tests/board_test.cpp tests/board_c6_test.cpp
git commit -m "about: print a board name instead of a revision number

Of the two boards left, one is a Watchy revision and one is this repo's
own design, so there is no shared numbering to print -- v6.0 would have
been nonsense. board.h names the board it built for, which also retires
the esp_chip_info probe that existed only to tell an S3 from an ESP32."
```

---

## Task 5: Bring the docs in line

`README.md` and `settings.h` still describe five boards, three RTC chips and an ESP32-S3, and still tell a reader to ignore the C6 env. Prose only — no behaviour changes.

**Files:**
- Modify: `RefCounter/settings.h`
- Modify: `README.md`

**Interfaces:**
- Consumes: the final state of `platformio.ini`, `board.h`, `RefRtc` and `RefClock` from Tasks 1-4. Every number and name quoted below matches those files.
- Produces: nothing consumed by later tasks — this is the last one.

- [ ] **Step 1: Fix the drift comment in `settings.h`**

Replace the opening of the `NTP_RESYNC_HOURS` comment block:

```c
// The RTC drifts a little every day - roughly a minute a month on the PCF8563
// fitted to V1.5 and V2, far less on the temperature-compensated DS3231 in
// V1.0. Re-syncing over WiFi keeps it well under a second, but a sync blocks
```

with:

```c
// The RTC drifts a little every day - roughly a minute a month on the PCF8563
// fitted to V2.0. Re-syncing over WiFi keeps it well under a second, but a
// sync blocks
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
a PCF8563 at 0x51 on the V2.0. It keeps running through low power mode and
```

Replace the drift sentence at the end of that section:

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

Then add a paragraph immediately after that one, because the C6 is now a supported target with no working clock and a reader deserves to know before they flash it:

```markdown
**The C6 board has no RTC driver yet.** Its RV-3028-C7 sits at 0x52 and
`RefRtc` does not know that address, so a C6 build finds no clock, the About
screen reports `RTC: none`, and the time survives only as long as the SoC has
power. Setting the time by hand or over NTP still works for that session. This
is a gap in the firmware, not in the board.
```

- [ ] **Step 3: Fix the Repo structure section of `README.md`**

Two lines in the tree block:

- `├── platformio.ini     build config; one env per Watchy revision` → `├── platformio.ini     build config; one env per supported board (V2.0, C6)`
- `    ├── RefRtc.h/.cpp      DS3231 / PCF8563 / ESP32-S3 clock, over I2C` → `    ├── RefRtc.h/.cpp      the PCF8563 clock, over I2C`

And the line for `RefClock`, which advertises a function this work deleted:

- `    ├── RefClock.h/.cpp    timekeeping, NTP sync, battery, board revision` → `    ├── RefClock.h/.cpp    timekeeping, NTP sync, battery`

Then, in *Dependencies*, replace:

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

- [ ] **Step 4: Rewrite the build instructions in `README.md`**

Immediately above the `### Option A — PlatformIO (recommended)` heading, add:

```markdown
This firmware supports two boards: the **Watchy V2.0** (ESP32) and **this
repo's own ESP32-C6-MINI-1 design** in [`board-files/`](board-files/). V1.0,
V1.5 and V3 were dropped — the V1 revisions put the up button and the battery
tap on other pins, V1.0 carries a different RTC chip entirely, and V3's ESP32-S3
target has been superseded by the C6 board. `board.h` stops the build if any of
their revision flags is set, rather than flashing a pin map built for other
hardware.
```

In *Option A*, replace:

```
on the first build. Plug the watch in, then build and flash. Pick the env for your revision —
`watchy_v2` (the default), `watchy_v15`, `watchy_v10`, or `watchy_v3`:
```

with:

```
on the first build. Plug the watch in, then build and flash. Pick the env for
your board — `watchy_v2` (the default) or `watchy_c6`:
```

and replace the C6 paragraph that follows:

```
There is a fifth env, `watchy_c6`, for this repo's own board design in
[`board-files/`](board-files/) — an ESP32-C6-MINI-1 with an RV-3028-C7. That
board has never been fabricated, and the env pulls a different PlatformIO
platform (the pioarduino fork) because the official one has no ESP32-C6
support. Ignore it unless you are working on the hardware.
```

with:

```
`watchy_c6` pulls a different PlatformIO platform — the pioarduino fork —
because the official `espressif32` tops out at Arduino-ESP32 2.0.17, which has
no ESP32-C6 support at all. Note that the board has never been fabricated and
its RV-3028-C7 has no driver in this firmware yet, so a C6 build cannot keep
time across a power cut.
```

- [ ] **Step 5: Fix the Arduino IDE section and the upload notes in `README.md`**

Replace step 3's first two paragraphs and code block:

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
   for the V2.0. The pin map is this project's own, so the board only has to
   be the right chip. (The C6 board is PlatformIO-only here; the IDE's ESP32-C6
   support depends on a 3.x core that this project does not otherwise require.)

   Then set the revision. The IDE has no build-flag field, so uncomment this
   line near the top of [`RefCounter/board.h`](RefCounter/board.h) — it has to
   go in that header, which every source file includes, not in the `.ino`,
   which would only set it for itself:

   ```c
   // #define ARDUINO_WATCHY_V20
   ```
```

Check the sentence after that code block, which begins `V3 needs nothing: selecting an S3 board defines...`, and replace it with:

```
   Under PlatformIO this is handled by `build_flags` and no edit is needed.
```

In *If upload fails*, replace the V3 paragraph:

```
On a **V3**, hold the **top-right (UP)** button while plugging in to force the
bootloader — on that revision UP is GPIO 0, the ESP32-S3 strapping pin. The
ESP32 revisions have no button on GPIO 0 and rely on the USB serial chip's
auto-reset instead, so there is no button to hold; if auto-reset is not
working, it is a cable or port problem rather than a timing one.
```

with:

```
On the **C6 board**, hold the **bottom-left (MENU)** button while plugging in
to force the bootloader — there MENU is GPIO 0, the ESP32-C6 strapping pin.
The V2.0 has no button on GPIO 0 and relies on the USB serial chip's
auto-reset instead, so there is no button to hold; if auto-reset is not
working, it is a cable or port problem rather than a timing one.
```

(That MENU/GPIO 0 pairing comes from `board.h`'s C6 block — `#define PIN_BTN_MENU 0`. Verify it against the file before writing it down.)

- [ ] **Step 6: Fix the Testing and Known limitations sections of `README.md`**

Append to the end of *Testing*, immediately before the `## Known limitations` heading:

```markdown
`tests/board_test.cpp` and `tests/board_c6_test.cpp` add a second kind of
check: they assert the V2.0 and C6 pin maps and each board's `BOARD_NAME`,
which until now nothing did for the C6 at all. `tests/run.sh` then compiles
`board.h` three more times, once per retired revision flag, to confirm each is
rejected rather than quietly building the V2.0 map.
```

In *Known limitations*, replace:

```
- **None of this has been run on a watch.** It builds clean for all four
  revisions and the time zone, digit-layout and sport-preset logic are tested
```

with:

```
- **None of this has been run on a watch.** It builds clean for both supported
  boards and the time zone, digit-layout and sport-preset logic are tested
```

- [ ] **Step 7: Verify no stale references survive**

Run:

```bash
grep -rniE "v1\.0|v1\.5|watchy_v10|watchy_v15|watchy_v3|ds3231|esp32-s3|esp32s3|boardrevision" README.md RefCounter tests platformio.ini
```

Expected hits, and nothing else:
- `RefCounter/board.h` — the guard comment and `#error`
- `tests/board_test.cpp`, `tests/board_c6_test.cpp` — comments and assert messages
- `tests/run.sh` — the `board_guard` loop
- `README.md` — the supported-boards note, the `board_test` paragraph, and any place the dropped boards are named as dropped

Anything else is a leftover. Fix it before finishing.

- [ ] **Step 8: Run the full verification**

Run: `./tests/run.sh` — expected: all green, exit 0.
Run: `pio run -e watchy_v2 -e watchy_c6` — expected: two `SUCCESS` lines.
Run: `pio project data | grep -i env` or `pio run --list-targets | head` — expected: only `watchy_v2` and `watchy_c6` appear.
Run: `git status --short` — expected: the `board-files/` modifications are still present and unstaged. Confirm no commit from this plan touched them: `git log --stat -6 -- board-files` should show nothing from today's work.

- [ ] **Step 9: Commit**

```bash
git add README.md RefCounter/settings.h
git commit -m "docs: describe the two supported boards

The README still walked a reader through building for V1.0, V1.5 and
V3, quoted the DS3231's drift figures, and told them to ignore the C6
env. Says what the firmware now supports, why the retired flags stop the
build, and that a C6 build has no working RTC yet."
```

---

## Verification

From the branch, with a clean-ish tree:

```bash
./tests/run.sh && pio run -e watchy_v2 -e watchy_c6
```

Both must succeed, and `run.sh`'s last block must print:

```
=== board_guard
  -D ARDUINO_WATCHY_V10 rejected
  -D ARDUINO_WATCHY_V15 rejected
  -D ARDUINO_ESP32S3_DEV rejected
```

Then confirm the retired envs are gone rather than merely unused:

```bash
pio run -e watchy_v3
```

Expected: `Unknown environment names 'watchy_v3'`, with `watchy_v2, watchy_c6` listed as the valid ones.

## Follow-up, not in this plan

`RefRtc` has no driver for the C6 board's RV-3028-C7 at 0x52, so a `watchy_c6`
build reports `RTC: none` and cannot keep time across a power cut. That gap
predates this work and is unchanged by it. It wants its own spec and plan.
