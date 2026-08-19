# NTP resync anchored to activity, not to the last sync

## Context

`NTP_RESYNC_HOURS` currently measures from the **last sync attempt**, and the
sketch separately refuses to sync until the watch has been idle for
`AUTO_SYNC_IDLE_MS` (60 s). The owner ships it set to `0` (manual only)
because a sync that lands mid-game blocks the watch for several seconds while
the radio comes up.

This change re-anchors the schedule to **user activity** and adds a second
knob for the minimum spacing between syncs, so the sync lands when the watch
is provably idle and no more than once a day:

- `NTP_RESYNC_HOURS = 3` — the quiet period. Sync once the watch has gone this
  long with no button press and no sleep entry. `0` still means manual only.
- `NTP_MIN_SYNC_INTERVAL_HOURS = 24` — the floor. Never sync more often than
  this, however long the watch is left alone. `0` means no floor.

A sync is due when **both** conditions hold:

```
dueAt = max(lastActivity + NTP_RESYNC_HOURS, lastSyncAttempt + NTP_MIN_SYNC_INTERVAL_HOURS)
```

The watch has two states this must work in:

- **Awake** (the idle light-sleep loop, where it spends most of its life):
  `idleTick()` syncs when `dueAt` has passed.
- **Deep asleep** (`enterSleep()` — hold top-left; only an ext1 button press
  wakes it): the deep sleep is additionally armed with a **timer wake** for
  `dueAt - now`. The watch wakes silently, syncs, and goes straight back to
  deep sleep without ever touching the panel, so the user never sees it.

Arming the timer with *time remaining* rather than a flat interval keeps one
rule across both states: sleeping ten minutes after a press wakes in 2 h 50 m,
not 3 h.

## Spec

The approved design, in full, is the Context section above plus these
decisions, which the implementer must follow exactly:

1. **Spacing keys off the last sync *attempt*, not the last success.** A
   failed sync (no WiFi) therefore pushes the next try out by
   `NTP_MIN_SYNC_INTERVAL_HOURS`. This is deliberate: it prevents a watch that
   cannot see its network from retrying in a loop with the radio up. Drift is
   about a minute a month, so a missed day costs nothing.
2. **A manual sync from the menu counts.** `RefMenu` calls
   `RefClock::syncFromNtp()`, which already stamps the attempt; the floor
   therefore applies to manual syncs for free. Do not add a separate path.
3. **RTC never set (`epoch() == 0`)** — there is no clock to measure a quiet
   period against. `syncDue()` returns true so the watch can self-heal, but
   **latched to one attempt per wake cycle** by a plain file-static (not
   `RTC_DATA_ATTR`) bool, so a watch with no WiFi does not spin the radio.
   `secondsUntilSyncDue()` returns `NEVER` in this case, so the deep-sleep
   timer is **not** armed — an unset clock cannot schedule anything.
4. **A press during the ~10 s resync window is lost.** If the sleep button is
   down when the resync wake finishes, the watch continues into a normal boot
   instead of re-sleeping; otherwise the press is swallowed and the user
   presses again. Accepted trade — do not build press queuing.
5. **`AUTO_SYNC_IDLE_MS` is deleted.** A 3-hour activity anchor subsumes a
   60-second one; two overlapping idle gates would be confusing.

## Global Constraints

- **Exact setting names and values:** `NTP_RESYNC_HOURS = 3`,
  `NTP_MIN_SYNC_INTERVAL_HOURS = 24`. Both `static const uint32_t` in
  `RefCounter/settings.h`, in the existing `--- Clock, menu and NTP ---`
  section, next to `NTP_SERVER` / `NTP_TIMEOUT_MS`.
- **House style is load-bearing.** This codebase comments the *why*, not the
  *what*, in full sentences, wrapped at 80 columns, in British-inflected prose
  ("behaviour", "colour"). Match `RefZone.cpp` and `RefClock.cpp`. Two-space
  indent, no tabs. Do not add a comment that merely restates the code.
- **Both verifications must pass before any commit:**
  - `./tests/run.sh` — host tests, all binaries, 0 failures.
  - `pio run -e watchy_v2` and `pio run -e watchy_v3` — both must reach
    `[SUCCESS]`. `watchy_v3` is the ESP32-S3 and compiles the
    `#ifdef ARDUINO_ESP32S3_DEV` branch of `deepSleepUntilButton()`, which the
    v2 build does not. A change to that function that only builds on v2 is a
    broken change.
- **No hardware is available.** On-watch behaviour cannot be verified by
  anyone in this loop. Do not claim it was. Say what was compiled and what was
  unit-tested, and leave the rest for the owner to flash.
- **Do not touch** `RefSport`, `RefSegments`, `RefZone`, `RefMenu`,
  `RefDisplay`, `RefPanel`, `Buttons`, `Buzzer`, or `RefRtc`. This change is
  confined to `settings.h`, `RefClock.*`, `RefCounter.ino`, one new module,
  one new test, `tests/run.sh`, and `README.md`.
- **YAGNI.** No settings beyond the two named. No backoff schedules, no retry
  ladders, no sync history, no new menu entries.

---

## Task 1: A pure, host-testable scheduling helper

`RefClock.cpp` pulls in `WiFi.h`, `Wire.h` and `esp_sntp.h`, so it cannot
compile on the host. The due-time arithmetic is the only part of this change
worth a unit test, so it goes in its own module — the same treatment
`RefZone`, `RefSegments` and `RefSport` already get.

### Create `RefCounter/RefSyncSchedule.h` and `RefCounter/RefSyncSchedule.cpp`

Follow the header style of `RefCounter/RefSegments.h` (include guard named
`REF_SYNC_SCHEDULE_H`, a file-level comment explaining why the module exists
separately, `namespace RefSyncSchedule`).

The API is exactly this — no more:

```c
namespace RefSyncSchedule {

// Returned when no automatic sync can be scheduled at all.
static const uint32_t NEVER = 0xFFFFFFFFUL;

// Seconds until the next automatic sync falls due, given the current UTC
// epoch, the epoch of the last button press or sleep entry, and the epoch of
// the last sync attempt. 0 means due now; NEVER means nothing to schedule.
uint32_t secondsUntilDue(time_t now, time_t lastActivity, time_t lastSyncAttempt,
                         uint32_t quietHours, uint32_t minIntervalHours);

} // namespace RefSyncSchedule
```

The hours are parameters rather than reads of `settings.h` so the test can
exercise combinations without recompiling the settings.

Required behaviour, in this precedence order:

1. `quietHours == 0` → `NEVER`. Automatic sync is off; `minIntervalHours` is
   irrelevant.
2. `now == 0` → `NEVER`. The RTC has never been set, so there is no clock to
   schedule against. (`RefClock` handles the self-heal case separately — see
   Task 2.)
3. `now < lastActivity` or `now < lastSyncAttempt` → `0`. The clock was set
   backwards; treat the sync as due rather than wait out a bogus interval.
4. Otherwise
   `dueAt = max(lastActivity + quietHours*3600, lastSyncAttempt + minIntervalHours*3600)`,
   and return `0` if `dueAt <= now`, else `dueAt - now`.

An anchor of `0` needs no special case: `0 + 24h` is far in the past for any
real `now`, so a never-synced watch is constrained only by the quiet period.
Do the arithmetic in `time_t`, not `uint32_t`, and cast only the returned
difference.

### Create `tests/sync_schedule.cpp`

Match the shape of `tests/segments_test.cpp`: a `failures` counter, an
`expect`-style helper that prints `ok`/`FAIL` with a label, `main()` returning
non-zero on failure, and a `PASSED (0 failures)` / `FAILED` summary line.

Cover, with `QUIET = 3` and `FLOOR = 24` unless the case says otherwise, and a
fixed fake `now` (e.g. `1750000000`) rather than the real clock:

- quiet period off (`quietHours = 0`) → `NEVER`, even with everything else due
- clock never set (`now = 0`) → `NEVER`
- pressed 2 h ago, never synced → `3600`
- pressed 3 h ago exactly, never synced → `0` (boundary is inclusive)
- pressed 4 h ago, synced 1 h ago → `23 * 3600` (the floor dominates)
- pressed 4 h ago, synced 25 h ago → `0` (both satisfied)
- pressed 10 min ago, synced 30 h ago → `2*3600 + 50*60` (the quiet period
  dominates)
- floor off (`minIntervalHours = 0`), pressed 3 h ago, synced 1 min ago → `0`
- clock set backwards (`lastActivity > now`) → `0`
- sync attempt in the future (`lastSyncAttempt > now`) → `0`

### Wire it into `tests/run.sh`

Add `[sync_schedule]="RefCounter/RefSyncSchedule.cpp"` to the `SOURCES` map
and `sync_schedule` to the `for t in ...` list. Update the file-header comment
that names which files are worth testing off the watch.

### Verification

`./tests/run.sh` — all five binaries pass. `pio run -e watchy_v2` still
succeeds (the new .cpp is now in the build). Commit.

---

## Task 2: Re-anchor `RefClock` and add the settings

### `RefCounter/settings.h`

Replace the current `NTP_RESYNC_HOURS` block (the `static const uint32_t
NTP_RESYNC_HOURS = 0;` line, its trailing owner comment about manual syncing,
and the comment paragraph above it) with the two settings.

Values are exactly `3` and `24`. The comments must explain the two-condition
rule and that the deep-sleep timer uses the same schedule — that is the part a
reader cannot infer.

**Leave `AUTO_SYNC_IDLE_MS` in place.** It is deleted in Task 3, in the same
commit that removes its only use in `RefCounter.ino`; deleting it here would
leave the sketch referencing an undeclared identifier and break this task's
own firmware build.

### `RefCounter/RefClock.h` / `.cpp`

Add a third `RTC_DATA_ATTR time_t lastActivity = 0;` beside the existing
`lastSyncAttempt` / `lastSyncOk` / `bootedAt`, and extend the comment above
that block — it currently explains that these survive deep sleep, which is
now load-bearing for the activity anchor too.

New and changed methods:

- `void noteActivity();` — stamps `lastActivity = _rtc.epoch()`. Called by the
  sketch on every button-driven state change and on sleep entry.
- `bool syncDue();` — keeps its name and its `bool` result. Now:
  - `NTP_RESYNC_HOURS == 0` → false.
  - RTC unset (`_rtc.epoch() == 0`) → true, but only once per wake cycle:
    guard with a **plain file-static** `bool unsetSyncTried = false` in the
    anonymous namespace (NOT `RTC_DATA_ATTR` — it must reset on every boot and
    every deep-sleep wake), set on the first true.
  - otherwise `secondsUntilSyncDue() == 0`.
- `uint32_t secondsUntilSyncDue();` — returns
  `RefSyncSchedule::secondsUntilDue(_rtc.epoch(), lastActivity, lastSyncAttempt,
  NTP_RESYNC_HOURS, NTP_MIN_SYNC_INTERVAL_HOURS)`. This is what the sketch arms
  the deep-sleep timer with.
- `RefClock::begin()` — stamp `lastActivity = _rtc.epoch()` **only when
  `lastActivity` is still 0**, so a first boot has an anchor. It must not
  restamp on a resync wake, which would push the quiet period out forever.

Update the doc comment on `syncDue()` in the header — it currently says
"True once NTP_RESYNC_HOURS have passed since the last attempt", which this
change makes wrong.

`syncFromNtp()` and `connectAndSync()` keep stamping `lastSyncAttempt`
unchanged. Do not make them stamp `lastActivity`.

### Verification

`pio run -e watchy_v2` and `pio run -e watchy_v3` both succeed;
`./tests/run.sh` still passes. Commit.

---

## Task 3: Drive it from the sketch, and update the README

### `RefCounter/RefCounter.ino`

- Delete the `static uint32_t lastActivityAt = 0;` declaration and its comment
  (around line 58). Its four assignment sites — in `enterIdle()`,
  `startTimer()`, the `busy` branch of `idleTick()`, and after the sync in
  `idleTick()` — become `refClock.noteActivity()`. Check for any site the
  grep in this plan missed.
- `enterSleep()` — add a `refClock.noteActivity()` before
  `RefDisplay::renderSleeping()`, so sleep entry anchors the schedule even
  though the hold that triggered it already did.
- `idleTick()` — the sync condition becomes just `if (refClock.syncDue())`,
  dropping the `(millis() - lastActivityAt) >= AUTO_SYNC_IDLE_MS` half.
  Update the comment above it: the hold-off is now hours of quiet, not one
  minute. Keep the `clockValid = false` re-read and the header repaint.
- **Delete `AUTO_SYNC_IDLE_MS` from `RefCounter/settings.h`**, along with the
  comment paragraph above it. Task 2 deliberately left it in place so that the
  sketch and the settings header stop referencing it in the same commit. The
  line above is its only use.
- `deepSleepUntilButton()` — after `esp_sleep_enable_ext1_wakeup(...)` and
  before `esp_deep_sleep_start()`, arm the resync timer:

  ```c
  const uint32_t untilSync = refClock.secondsUntilSyncDue();
  if (untilSync != RefSyncSchedule::NEVER) {
    // Clamp: an already-due sync would otherwise ask for a zero length timer.
    const uint32_t secs = untilSync < 60 ? 60 : untilSync;
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  }
  ```

  Note this runs *after* `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`,
  which is why it cannot move earlier. Comment why the timer is there at all —
  a reader seeing a timer on a "sleep until button" function deserves the
  reason.
- `setup()` — branch on the wake cause. The resync wake must not bring the
  panel up: the SLEEPING screen is already on the e-paper and must stay there.

  ```c
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const bool resyncWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
  ...
  if (!resyncWake) {
    RefDisplay::begin();
  }
  refClock.begin(cause == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (resyncWake) {
    refClock.connectAndSync();
    if (!Buttons::anyDown()) {
      deepSleepUntilButton(); // never returns
    }
    RefDisplay::begin(); // woken mid-sync; carry on into a normal start
  }
  ```

  Keep `Buzzer::begin()`, `Buttons::begin()` and `RefSport::begin()` on both
  paths — `Buttons::anyDown()` needs the pins configured.
- Add `#include "RefSyncSchedule.h"` for `NEVER`.

### `README.md`

Two places describe the old behaviour: the prose paragraph around line 190
("`NTP_RESYNC_HOURS` (24 by default) once WiFi credentials have been
saved… Set `NTP_RESYNC_HOURS` to 0…") and the settings listing around line
318 (`static const uint32_t NTP_RESYNC_HOURS = 24; // 0 = only sync by hand`).
Find them by grep rather than by line number.

Both must describe: the two-condition rule, the two settings with their real
defaults, that sleep entry counts as activity, and that the watch wakes itself
from deep sleep to sync and goes straight back without lighting the panel.
Drop the `AUTO_SYNC_IDLE_MS` mention. Match the README's existing voice and
column width.

### Verification

`./tests/run.sh`; `pio run -e watchy_v2`; `pio run -e watchy_v3`. All three
must pass — the S3 build is the one that compiles the other half of
`deepSleepUntilButton()`. Commit.
