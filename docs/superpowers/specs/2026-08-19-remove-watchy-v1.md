# Drop Watchy V1.0 and V1.5 Support

**Date:** 2026-08-19
**Status:** accepted

## Problem

The sketch supports four Watchy revisions: V1.0, V1.5, V2.0 (all ESP32) and
V3 (ESP32-S3). The two v1 revisions cost real complexity for hardware nobody
here is running:

- `board.h` carries a three-way `#if` over the up button and the battery tap,
  because both pins moved between V1.0, V1.5 and V2.0.
- `RefRtc` carries a whole second RTC driver — the DS3231, fitted only to
  V1.0 — including its own BCD decode, register write and probe. V1.5 and V2.0
  both ship the PCF8563, so V1.5 buys nothing here at all.
- `RefClock::boardRevision()` cannot tell V1.5 from V2.0 by probing (same
  chip), so it falls back to a build-time `#if` to pick between them.
- `platformio.ini` carries two extra envs, and the docs carry the matching
  two extra sets of instructions.

None of it has ever been run on v1 hardware — see the README's *Known
limitations* — so this is untested code carrying untested branches.

## Decision

Remove V1.0 and V1.5 entirely. The supported set becomes:

| Revision | Env | SoC | RTC |
| --- | --- | --- | --- |
| V2.0 | `watchy_v2` (default) | ESP32 | PCF8563 @ 0x51 |
| V3 | `watchy_v3` | ESP32-S3 | internal, 32kHz crystal |

## Requirements

1. **Envs.** `[env:watchy_v10]` and `[env:watchy_v15]` are gone from
   `platformio.ini`. `watchy_v2` stays the default env.

2. **Pin map.** `board.h`'s ESP32 branch is unconditional and holds the V2.0
   numbers: `PIN_BTN_UP 35`, `PIN_BATT_ADC 34`. The per-revision `#if` over
   those two goes away.

3. **Retired flags fail the build.** Building with `-D ARDUINO_WATCHY_V10` or
   `-D ARDUINO_WATCHY_V15` — from a stale build flag, or an Arduino IDE user's
   leftover `#define` in `board.h` — must stop the compile with a message
   naming the supported envs. It must *not* silently fall through to the V2.0
   map: on v1 hardware the battery would be read off the wrong pin and the up
   button would never be seen. The exact text:

   ```
   Watchy V1.0 and V1.5 are no longer supported; build watchy_v2 or watchy_v3
   ```

4. **DS3231 gone.** `RefRtc::Kind` loses `DS3231`; the probe, the register
   read, the register write and the About screen's label for it all go with
   it. The PCF8563 and the S3's internal clock are the whole set.

5. **Board revision.** `RefClock::boardRevision()` returns 30 for an S3, 20
   for an ESP32 with a PCF8563 found, and 0 when no clock was found. No
   build-time `#if` is involved.

6. **Docs match.** `README.md`, `settings.h` and `RefClock.h` carry no
   instructions, pin numbers, drift figures or chip references for V1.0 or
   V1.5. The build instructions offer two envs, and the Arduino IDE section
   offers one `#define`.

7. **Tests.** The host test suite pins the V2.0 pin map and the surviving
   `RefRtc::Kind` values, and proves that each retired flag is rejected with
   the message from requirement 3.

## Out of scope

- `board-files/` (the in-progress S3-MINI board) is untouched; it has never
  had a v1 variant.
- Existing plan and spec documents under `docs/superpowers/` are historical
  records of work already done and are not rewritten. Only this spec and the
  plan that implements it describe the new supported set.
- No runtime behaviour changes for V2.0 or V3 users. This is a removal, not a
  refactor: every surviving line keeps the value it has today.
