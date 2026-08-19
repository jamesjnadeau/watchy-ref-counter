# Drop Watchy V1.0, V1.5 and V3 Support

**Date:** 2026-08-19
**Status:** accepted

## Problem

The firmware supports five boards: Watchy V1.0, V1.5, V2.0 (ESP32), V3
(ESP32-S3), and this repo's own ESP32-C6-MINI-1 design in `board-files/`.
Three of those five are dead weight:

- **V1.0 and V1.5** cost `board.h` a three-way `#if` over the up button and the
  battery tap, and cost `RefRtc` a whole second driver — the DS3231, fitted
  only to V1.0, with its own BCD decode, register write and probe. V1.5 ships
  the same PCF8563 as V2.0, so it buys nothing at all beyond a `#if` in
  `RefClock::boardRevision()` to tell the two apart.
- **V3** is a stock ESP32-S3 dev board target that this repo's own C6 board has
  superseded. It carries its own pin map, its own `RefRtc` branch for the S3's
  internal clock, and it is the flag guarding two blocks that the C6 actually
  needs.

None of the five has ever been run on hardware — see the README's *Known
limitations* — so the three being removed are untested code carrying untested
branches.

The V3 flag also masks two live defects on the C6:

1. `RefPanel::begin()` calls `SPI.begin(SCK, MISO, MOSI, SS)` under
   `#ifdef ARDUINO_ESP32S3_DEV`. The C6's SPI pins are 15/23/14/8, not the
   default bus, so the C6 needs that call and does not get it. The panel would
   not talk.
2. `RefCounter.ino`'s deep-sleep block and `RefRtc::begin()` both branch on the
   S3 flag, so removing V3 without touching them changes C6 behaviour by
   accident.

## Decision

Remove V1.0, V1.5 and V3. The supported set becomes:

| Board | Env | SoC | RTC |
| --- | --- | --- | --- |
| Watchy V2.0 | `watchy_v2` (default) | ESP32 | PCF8563 @ 0x51 |
| This repo's board | `watchy_c6` | ESP32-C6-MINI-1 | RV-3028-C7 (see *Known gap*) |

## Requirements

1. **Envs.** `[env:watchy_v10]`, `[env:watchy_v15]` and `[env:watchy_v3]` are
   gone from `platformio.ini`. `watchy_v2` stays the default env; `watchy_c6`
   keeps its pinned pioarduino platform.

2. **Pin maps.** `board.h` holds exactly two: the C6 map under
   `ARDUINO_ESP32C6_DEV` and the V2.0 map under everything else. The V2.0 map
   is unconditional — `PIN_BTN_UP 35`, `PIN_BATT_ADC 34` — with no
   per-revision `#if`.

3. **Retired flags fail the build.** `-D ARDUINO_WATCHY_V10`,
   `-D ARDUINO_WATCHY_V15` and `-D ARDUINO_ESP32S3_DEV` must each stop the
   compile with a message naming the supported envs. They must *not* fall
   through to the V2.0 map: on that hardware the battery would be read off the
   wrong pin and the up button would never be seen, and an S3 has no GPIO 26 or
   35 at all. The exact text:

   ```
   Watchy V1.0, V1.5 and V3 are no longer supported; build watchy_v2 or watchy_c6
   ```

   `ARDUINO_ESP32S3_DEV` matters most here, because it comes from the board
   choice rather than from a build flag a user typed.

4. **RTC kinds.** `RefRtc::Kind` becomes `{NONE, PCF8563}`. `DS3231` goes with
   V1.0 and `INTERNAL` — the S3's own 32kHz-backed clock — goes with V3.

5. **The C6 keeps the blocks it needs.** Every `ARDUINO_ESP32S3_DEV`
   conditional is resolved deliberately, not deleted wholesale:
   - `RefPanel::begin()`'s `SPI.begin(...)` becomes `#ifdef ARDUINO_ESP32C6_DEV`
     — a fix, not a rename: the C6 needs it and never had it.
   - `RefCounter.ino`'s `rtc_gpio_*` deep-sleep block becomes
     `#ifdef ARDUINO_ESP32C6_DEV`, keeping today's behaviour for both boards.
   - `RefRtc::begin()`'s `INTERNAL` branch is deleted, so the C6 probes I2C
     like V2.0 does.

6. **Board identity.** `RefClock::boardRevision()` returned 10/15/20/30 and the
   About screen printed it as `v2.0`. Two boards remain and one of them has no
   revision number, so the number is replaced by a name: `board.h` defines
   `BOARD_NAME` as `"V2.0"` or `"C6"`, the About screen prints it directly, and
   `boardRevision()` and its `esp_chip_info` probe are deleted.

7. **Docs match.** `README.md` and `settings.h` carry no instructions, pin
   numbers, drift figures or chip references for V1.0, V1.5 or V3. The C6 stops
   being described as "a fifth env … ignore it" and becomes one of the two
   supported targets.

8. **Tests.** The host suite pins both surviving pin maps and both `BOARD_NAME`
   values, pins the surviving `RefRtc::Kind` values, and proves each retired
   flag is rejected with the message from requirement 3. The C6 map has never
   had a test — `board.h` says so itself — and gets one here.

## Known gap — the C6 has no RTC driver

> **Closed 2026-08-19**, after this spec's work landed. `RefRtc` now drives the
> RV-3028-C7 at 0x52 and a C6 build keeps time. The section is left as written
> because it is what the removal was scoped against; read it as history.

The C6 board carries an RV-3028-C7 at 0x52. `RefRtc` has never known about it:
today the C6 build probes for a DS3231 and a PCF8563, finds neither, and lands
on `Kind::NONE`, which means no timekeeping at all. That is the state before
this work and the state after it.

It is called out here because after this change the C6 is one of only two
supported boards, so the gap is more visible — but adding a driver is a
feature, not part of a removal, and it is **out of scope for this spec**. It
wants its own spec and plan.

## Out of scope

- The RV-3028-C7 driver, per above.
- `board-files/` — the hardware design is untouched.
- Existing plan and spec documents under `docs/superpowers/` are historical
  records of work already done and are not rewritten.
- No runtime behaviour changes for V2.0 or C6 beyond the `SPI.begin()` fix in
  requirement 5, which is a bug fix the removal forces into the open.
