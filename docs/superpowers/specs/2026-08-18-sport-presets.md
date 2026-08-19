# Sport Timing Presets — Spec

**Date:** 2026-08-18
**Status:** approved for planning

## Problem

The watch is a football play clock with two hard-coded countdowns, 40s and 25s,
baked into `settings.h` at compile time. Officials who work more than one sport
need different pairs of values, and changing them today means a reflash.

[ReadyRef](https://ready-ref.com/personal-timers/) sells one physical timer per
sport for exactly this reason. Their lineup, from
<https://ready-ref.com/store_timers/>:

| Sport | Model | Intervals | Pre-warnings |
| --- | --- | --- | --- |
| Football | 1128 | 40 & 25 | 30s elapsed |
| Football | 1132 | 40 & 25 | 25s & 30s elapsed |
| Lacrosse | 1150 / 1155 | 20 & 120 | — |
| Baseball NCAA | 1140 | 20 & 120 | — |
| Baseball NFHS | 1141 | 20 & 80 | — |
| Softball NCAA | 1145 | 20 & 90 | — |
| Softball NFHS | 1146 | 20 & 60 | — |
| Universal | 1190 / 1195 | switches football ↔ lacrosse | — |

One Watchy should cover all of them, selected from the on-watch menu, plus a
Custom slot the user can dial in themselves.

## Goals

1. A **Sport** entry in the settings menu that picks a preset. The choice
   persists in NVS across a reflash, the way TZ and DST already do.
2. Ship the seven presets in the table below.
3. A **Custom** preset whose numbers are editable from the menu.
4. Support countdowns above 99 seconds, which the two-digit display cannot
   currently show.

## Non-goals

- Per-sport buzz *durations*. `BUZZ_SHORT_MS`, `BUZZ_EXPIRE_MS`,
  `BUZZ_CONFIRM_MS`, `BUZZ_GAP_MS`, the hold thresholds, `DARK_MODE`, the time
  zone and everything else in `settings.h` stay global.
- More than one custom slot.
- Any period/game clock, penalty timer or score keeping. This stays a two-button
  interval timer.
- Hardware validation. Nothing in this repo has run on a watch (see README
  *Known limitations*); this change does not alter that.

## Decisions

These were settled with the user before planning and are not open:

**D1 — Values over 99 render with a skinny leading `1`.** The two existing
full-size seven-segment digits are kept at their current size and shifted right
to make room for a hundreds bar at the left edge. `120` reads `1|2|0`. Crossing
100 → 99 re-centres the pair, which is accepted. **Maximum value is 199**, and
the Custom editor clamps to it.

**D2 — A preset carries clocks and warning marks only.** Specifically: long
seconds, short seconds, two early-warning marks, and the final-countdown-from
value. Buzz counts stay global (`WARNING_BUZZ_COUNT` for the first mark, a new
`WARNING_BUZZ_COUNT_2` for the second).

**D3 — One editable Custom slot.** Fixed presets are read-only; `Custom` is the
only editable one, with its factory values in `settings.h`.

**D4 — Ship all seven presets** (six sports plus Custom).

## Preset table

Warning marks are expressed in **seconds remaining**, matching the existing
`WARNING_AT_SECONDS` convention. ReadyRef quotes pre-warnings in seconds
*elapsed*; on a 40s clock their "30 second pre-warning" is 10 remaining, which
is what the firmware already does today.

`0` disables a mark. A mark larger than a clock's duration simply never fires,
which is how the 30-remaining warning stays out of the way on a 20s clock.

| Name | Description | Long | Short | Warn 1 | Warn 2 | Final from |
| --- | --- | --- | --- | --- | --- | --- |
| `Football` | NFHS/NCAA football | 40 | 25 | 10 | 0 | 5 |
| `Lacrosse` | Lacrosse shot clock | 120 | 20 | 30 | 10 | 5 |
| `Base NCAA` | NCAA baseball | 120 | 20 | 30 | 10 | 5 |
| `Base NFHS` | NFHS baseball | 80 | 20 | 30 | 10 | 5 |
| `Soft NCAA` | NCAA softball | 90 | 20 | 30 | 10 | 5 |
| `Soft NFHS` | NFHS softball | 60 | 20 | 20 | 10 | 5 |
| `Custom` | User set | 40 | 25 | 10 | 0 | 5 |

Football's row reproduces today's shipped behaviour exactly, so an existing
watch that has never opened the menu behaves identically after this change.
Model 1132's second pre-warning (15 remaining) is reachable through Custom.

Names are capped at **9 characters** so the menu row `Sport: Base NCAA` fits the
200px panel in `FreeMonoBold9pt7b` (11px per glyph, 16 glyphs = 176px).
Descriptions are capped at **14 characters**. `pickSport` draws the description
and the position counter on the same baseline (y=180); with 7 presets the
counter is always 3 glyphs, right-aligned starting at x=165. A description of N
glyphs starts at x=2 and ends at x=2+11N, so 14 glyphs (ending at 156) is the
last width that stays clear of the counter — 15 would already touch it.

## Behaviour

### Selecting a sport

A `Sport: <name>` row in the main menu opens a scrolling picker modelled on the
existing time-zone picker: UP/DOWN moves, MENU selects and persists, BACK leaves
the stored value alone, and the screen times out after `MENU_TIMEOUT_MS`. Each
row shows the name and its two clock values; the footer shows the highlighted
preset's description and its position in the list.

The main menu grows from 7 entries to 9, which no longer fits on the panel at
`MENU_ROW_H` = 25. The main menu therefore gains the same 7-row scrolling window
the zone picker already uses.

### Editing Custom

An `Edit Custom` row opens a five-field editor. Fields, in order: Long, Short,
Warn 1, Warn 2, Final from. MENU advances and commits past the last field, BACK
steps back, UP/DOWN change the value under the cursor, the active row blinks.
This mirrors the Set Time screen's interaction exactly.

Values are shown as text, not seven-segment digits — five labelled rows are more
legible than five digit pairs, and it keeps new segment geometry out of the menu.

Clamping: Long and Short are 1..199. Warn 1, Warn 2 and Final-from are 0..199,
where 0 disables. Editing Custom does not select it; the user picks `Custom` from
the Sport picker to make it active.

### Running

The active preset supplies the two button durations and every buzz mark:

- Top-right hold starts `longSeconds`; bottom-right starts `shortSeconds`.
- At `warnAtSeconds` remaining: `WARNING_BUZZ_COUNT` short buzzes.
- At `warn2AtSeconds` remaining: `WARNING_BUZZ_COUNT_2` short buzzes.
- At or below `finalCountdownFrom`: one short buzz per second.
- At 0: `BUZZ_EXPIRE_MS`.

Precedence, highest first: expire, final countdown, warn 1, warn 2. A mark set
inside `finalCountdownFrom` is silently swallowed by the per-second countdown,
which is the behaviour today.

### Screens

**Ready** stacks the active preset's two values, as now, and its footer shows the
active sport name in caps instead of `HOLD TO START`. That is the confirmation
an official needs before kickoff, and there is no room for both.

**Running** shows the countdown with the leading-`1` rule from D1.

## Constraints

- No new libraries. GxEPD2, Adafruit GFX and WiFiManager remain the only three.
- New persistence follows the `RefZone` pattern: a `Preferences` handle opened
  and closed per read/write, defaults from `settings.h` when nothing is stored.
- The tick-path partial-refresh window must cover the widest layout the
  countdown can take, or a stale hundreds digit is left standing on the panel.
- `RefSport.cpp` must compile on a host against `tests/stub/Preferences.h`, so
  the preset table and clamping are testable off the watch alongside `RefZone`.
