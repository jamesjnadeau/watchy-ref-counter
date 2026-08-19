Intial prompt:

Based on the firmware available here: 

https://github.com/sqfmi/Watchy

I have clone the repo locally at ../Watchy

I'm creating a purpose built countdown timer used for reffing a football game. 

The user should see a countdown on the screen that updates every second. 
The count down should have 2 configurable start timers, start with 40 and 25 as defaults.
The user should use the two buttons on the right hand side of the device to start the timers. The top one should be the longer value
The timer buttons should have a configurable hold down before it acts as a reset of the timer, or to start the timer in general. Make it be 0.5 seconds as a default.

When the timer is up, it should buzz a configurable time, start with 1 second. 
Before the timer reaches a configurable number of seconds, default with 10 seconds, it should buzz a configurable number of times, start with 1. By default it should buzz once at 10 seconds. 

It should count down the last 5 seconds by buzzing at each second mark. So it should buzz at 5, 4, 3, 2, and 1, then do the finall configuable buzz time.
The top left button, after being held down for a configurable ammount of time, different than the above one, default 1 second, should then put the device into low power mode. The device should wake back up to normal operating mode if this button is pressed again, If in lower power mode, it should say sleeping on the display.

Please create a read me seciton below the line that documents the structure of the repo and how to install it on the device.

---

# Watchy Ref Counter

A countdown timer for sports officials, running on a
[Watchy](https://watchy.sqfmi.com) e-paper watch. Seven presets — football,
lacrosse, NCAA and NFHS baseball, NCAA and NFHS softball, and a custom slot —
each with two countdowns, big 7-segment digits, and a buzz pattern you can
follow without looking at your wrist.

**This repo stands alone.** It does not link against the
[Watchy library](https://github.com/sqfmi/Watchy); the panel is driven through
GxEPD2, the RTC chips directly over I2C, and NTP through the ESP32 core's own
SNTP client. Where the behaviour follows that firmware — the menu, the
set-time screen, the pin map — the comments credit it, but none of its code is
here. It builds with the reference repo nowhere on disk.

## Controls

Watchy's four buttons, by physical position:

```
   BACK (top left)  ----+---------+---- UP   (top right)
                        |  200 x  |
   MENU (bottom left) --|  200    |---- DOWN (bottom right)
                        +---------+
```

Watch out for the names: on Watchy, **MENU is the bottom-left button and BACK
is the top-left one**, which is the opposite of what they sound like.

| Button | Action | Default hold |
| --- | --- | --- |
| Top right | Start, or reset, the active preset's **long** clock | 0.5s |
| Bottom right | Start, or reset, the active preset's **short** clock | 0.5s |
| Top left | Enter low power mode; hold again to wake | 1.0s |
| Bottom left | On Ready, open the settings menu; otherwise clear the clock | 0.5s |

The two right-hand buttons always run whichever preset is active — see
*Sports* below — rather than a fixed 40 and 25 seconds.

The bottom-left button does two jobs because on the Ready screen there is no
clock to clear. Hold it there and you get the reference Watchy menu; hold it
while a clock is up and it clears back to Ready.

**A short tap does nothing.** Every action needs a deliberate hold, so a button
brushed against a sleeve mid-game cannot reset the play clock. A short
confirmation buzz fires the instant a hold registers, so you know the clock
started without looking down.

Holding a timer button while that clock is already running restarts it from
its full value.

## Sports

The right-hand buttons run whichever preset is active. Seven ship in the box
— six fixed sports plus one editable Custom slot:

| Name | Description |
| --- | --- |
| Football | football |
| Lacrosse | lacrosse shot |
| Base NCAA | NCAA baseball |
| Base NFHS | NFHS baseball |
| Soft NCAA | NCAA softball |
| Soft NFHS | NFHS softball |
| Custom | user set |

Their clock lengths and buzz marks are in *Buzz pattern*, below. `Football`
reproduces exactly what this firmware shipped with before presets existed, so
a watch that has never opened the menu behaves the same as it always did.

The active choice comes from the menu's `Sport` row, which opens a scrolling
picker: UP/DOWN moves, MENU selects and persists, BACK leaves the stored
choice alone. Like the time zone, it is kept in NVS, so it survives a
reflash — the sport a watch is set to is a property of who is carrying it,
not of the firmware on it.

Each row shows a preset's name and its two clock values; the footer below
shows the highlighted preset's description, its position in the list, and its
two warning marks as `warn <first>/<second>`. The final-countdown value isn't
there — it's 5 for every fixed preset, so it never tells two of them apart,
and it's shown spelled out on the Custom editor instead, the one preset that
can change it.

The six fixed presets are read-only. `Edit Custom` opens the one preset that
can be changed: five fields — Long, Short, Warn 1, Warn 2, Final — with
MENU stepping to the next field and committing once past the last one, BACK
stepping back, and UP/DOWN changing the value under the cursor, the same
interaction Set Time uses. The two clocks clamp to 1–199 seconds; the three
marks clamp to 0–199, where 0 turns that mark off. Editing Custom does not
select it — pick `Custom` from the `Sport` screen to make it active, the same
separation the zone picker keeps between "known" and "in use".

## Buzz pattern

Every preset buzzes on the same four events, at the marks that preset sets:

| Event | Buzz |
| --- | --- |
| First warning, at `warnAtSeconds` remaining | `WARNING_BUZZ_COUNT` (1) short buzz |
| Second warning, at `warn2AtSeconds` remaining | `WARNING_BUZZ_COUNT_2` (2) short buzzes |
| Final countdown, at or below `finalCountdownFrom` | 1 short buzz (150ms) on each second mark |
| Expire, at 0 | 1 long buzz (1000ms) |

A mark of 0 turns it off — Football's second warning ships off, which is why
the stock clock only ever buzzes once early. A mark that falls at or inside
the final countdown is silently swallowed by the per-second buzz, which
already covers it. The two buzz counts and the expire length are global, in
`settings.h`; the marks themselves are per-preset, in `RefSport.cpp`:

| Name | Long | Short | Warn 1 | Warn 2 | Final from |
| --- | --- | --- | --- | --- | --- |
| Football | 40 | 25 | 10 | – | 5 |
| Lacrosse | 120 | 20 | 30 | 10 | 5 |
| Base NCAA | 120 | 20 | 30 | 10 | 5 |
| Base NFHS | 80 | 20 | 30 | 10 | 5 |
| Soft NCAA | 90 | 20 | 30 | 10 | 5 |
| Soft NFHS | 60 | 20 | 20 | 10 | 5 |
| Custom | 40 | 25 | 10 | – | 5 |

Every mark above is in seconds **remaining**, matching what the clock counts
down. This is worth being deliberate about, because
[ReadyRef](https://ready-ref.com/personal-timers/) — the commercial timers
this preset table is modelled on — quote their pre-warnings in seconds
*elapsed*. Their football timer's "30 second pre-warning" on a 40 second
clock is the same moment as Football's `Warn 1` of 10 above: 30 seconds in is
10 seconds left. Getting that backwards would fire the warning at the wrong
end of the play.

Buzzes are deliberately blocking, and fire *before* the screen redraws: the
buzz is the cue you actually react to, and an e-paper refresh takes most of a
second. This costs no accuracy, because remaining time is always recomputed
from the timestamp the clock started at rather than accumulated tick by tick.

## Screens

Every screen carries the wall clock at the top left and a battery gauge at the
top right.

**Ready** — shown at boot and on wake. Both of the active preset's clocks
stacked to line up with the two right-hand buttons that start them. The
footer shows the active sport's name, in capitals, in place of `HOLD TO
START` — the thing worth checking before kickoff is which preset is loaded,
not how the buttons work.

**Running** — the current clock, large, updating every second. The header is
the same wall clock and battery gauge every screen carries; it does not say
which of the two clocks is counting.

**Time expired** — `00` holds for `EXPIRED_HOLD_MS` (3s), long enough to
register the delay of game, then the watch drops back to Ready on its own.
Starting a new clock during that window cancels the return.

**Sleeping** — low power mode.

**Menu** — held open from Ready. Navigation follows the reference: UP/DOWN to
move, MENU (bottom left) to select, BACK (top left) to leave. It also drops
back to Ready by itself after `MENU_TIMEOUT_MS`, so a menu opened by accident
cannot strand you mid-game. Nine entries no longer fit the panel at once, so
the list scrolls a seven-row window, keeping the highlighted entry in view the
same way the time zone picker already does.

Every screen the menu opens times out on the same rule, and a timeout always
**discards**: Set Time leaves the clock alone, Edit Custom leaves the stored
preset alone, and the two pickers leave their stored choice alone. Only
pressing MENU past the last field commits. This matters more than it sounds —
these screens repaint about twice a second, so one left open would otherwise
hold the panel refreshing until the battery went flat.

| Entry | What it does |
| --- | --- |
| Sport: *name* | Pick one of the seven presets from a scrolling list |
| Edit Custom | Set the five Custom fields by hand |
| About | Version, board revision, battery, time, uptime, last sync, RTC type |
| Vibrate Motor | Buzz test |
| Set Time | Set the clock by hand, no WiFi needed |
| TZ: *name* | Pick a US time zone from a scrolling list |
| DST: Auto/Off | Turn the daylight saving rule on or off |
| Setup WiFi | Captive portal to save credentials |
| Sync NTP | Connect and set the clock from `NTP_SERVER` |

The **Sport**, **TZ** and **DST** rows show their current value rather than
fixed text, so the menu doubles as the status display for all three. **DST**
toggles in place and redraws with a fast partial refresh; everything else
opens a screen.

There is no "Show Accelerometer". The reference has one, but nothing on a play
clock reads the sensor, and leaving it out means the BMA423 is never powered
up at all.

## Time zones and daylight saving

The RTC holds UTC. The zone offset is applied on the way out, every time the
clock is read, which is what makes the two menu entries cheap: changing zone or
flipping the DST switch takes effect on the next redraw and never rewrites the
clock chip.

The **TZ** entry lists all eleven US zones by name, with their standard offset
on the row and the abbreviations and region underneath:

| Zone | Standard | Daylight | Where |
| --- | --- | --- | --- |
| Eastern | UTC−5 EST | EDT | NY, DC, Atlanta, Miami |
| Central | UTC−6 CST | CDT | Chicago, Dallas, Minneapolis |
| Mountain | UTC−7 MST | MDT | Denver, Salt Lake, Albuquerque |
| Arizona | UTC−7 MST | — | most of Arizona |
| Pacific | UTC−8 PST | PDT | LA, Seattle, SF, Las Vegas |
| Alaska | UTC−9 AKST | AKDT | most of Alaska |
| Aleutian | UTC−10 HST | HDT | western Aleutians |
| Hawaii | UTC−10 HST | — | Hawaii |
| Samoa | UTC−11 SST | — | American Samoa |
| Atlantic | UTC−4 AST | — | Puerto Rico, USVI |
| Chamorro | UTC+10 ChST | — | Guam, N. Marianas |

**DST: Auto** means the US federal rule is applied when the date calls for it —
forward at 02:00 local standard on the second Sunday in March, back at 02:00
local daylight on the first Sunday in November — not that the clock is shifted
year round. Nothing needs doing at a changeover: the offset is computed from
the instant being displayed, so the watch is right on both sides of it and
right again if it is rebooted or asleep across it. **DST: Off** pins the zone
to standard time all year. The five zones with no daylight saving ignore the
switch either way.

Both settings are stored in NVS, so they survive a reflash — the zone is a
property of where the watch is, not of the firmware on it. `DEFAULT_TIME_ZONE`
and `DEFAULT_DST_AUTO` in `settings.h` are only the starting point, used until
the menu has been used once.

The rule is the one in force since 2007 (Energy Policy Act of 2005). It is
verified against the real transition dates for 2024–2027 and 2030 in every
zone; see *Testing* below. Set Time is exact except for the one hour a year
that springing forward deletes — typing 02:30 on that morning stores 01:30,
because 02:30 never happens.

## Timekeeping

The time comes from the watch's RTC chip, which `RefRtc` probes for at boot —
DS3231 at 0x68 on V1.0, PCF8563 at 0x51 on V1.5 and V2, and on V3 the
ESP32-S3's own 32kHz-backed clock. It keeps running through low power mode and
through a reflash, so the watch only needs setting once. The About screen
reports which one was found.

The chip is kept in UTC, not local time. That is what lets the zone and DST
settings change with no clock write and no drift, and it means an NTP sync
needs no offset applied to it.

Drift is handled two ways. **Set Time** sets it by hand, in local time.
**Sync NTP** sets it from the internet, on demand from the menu. It can also
run automatically every `NTP_RESYNC_HOURS` once WiFi credentials have been
saved — but the shipped default is 0, which means never automatically at
all. That is deliberate: the author would rather sync by hand than have an
automatic one land in the middle of a game. Set it to a nonzero value and
reflash to turn automatic resync on. A PCF8563 drifts roughly a minute a
month, so even a daily sync keeps that under a second. The DS3231 in V1.0 is
temperature compensated and drifts far less.

An automatic sync blocks for several seconds while the radio comes up, so
when enabled it is held off until the watch has been left untouched for
`AUTO_SYNC_IDLE_MS` (60s), and it never runs while a clock is counting.

The header clock is repainted on its own partial window when the minute rolls
over, so keeping it live costs one ~400ms refresh a minute rather than a whole
screen. It is deliberately **not** updated while a clock is counting down: the
tick path stays reserved for the countdown, so the time can sit stale for as
long as the active clock runs — up to 199s on Custom at its ceiling — and
catches up as soon as the clock stops.

## Repo structure

```
watchy-ref-counter/
├── README.md          this file
├── platformio.ini     build config; one env per Watchy revision
└── RefCounter/        the sketch (also a valid Arduino IDE sketch folder)
    ├── RefCounter.ino main loop and the IDLE/RUNNING/EXPIRED/SLEEPING states
    ├── settings.h     every tunable number, and nothing else
    ├── board.h        the pin map, per revision, and button polarity
    ├── Buttons.h/.cpp debounced reads with one-shot hold detection
    ├── Buzzer.h/.cpp  vibration motor
    ├── RefPanel.h/.cpp    the GxEPD2 panel instance and its pins
    ├── RefDisplay.h/.cpp  screen layout and e-paper refresh strategy
    ├── RefRtc.h/.cpp      DS3231 / PCF8563 / ESP32-S3 clock, over I2C
    ├── RefClock.h/.cpp    timekeeping, NTP sync, battery, board revision
    ├── RefZone.h/.cpp     US time zones and the daylight saving rule
    ├── RefSegments.h/.cpp  where the countdown digits land, host-tested
    ├── RefSport.h/.cpp     sport presets and the custom slot
    └── RefMenu.h/.cpp     the settings menu
```

### Dependencies

Three libraries, all from the registry:

| Library | For |
| --- | --- |
| GxEPD2 | the GDEH0154D67 panel, including fast partial update |
| Adafruit GFX | fonts and primitives |
| WiFiManager | the captive portal behind "Setup WiFi" |

Everything else is either this project's own or comes with the ESP32 Arduino
core. Notably there is **no RTC library**: the DS3231 and PCF8563 are a handful
of BCD registers each, and talking to them directly avoids two dependencies
that both caused trouble — one whose `master` no longer compiles against a
current core, and one that `#define`s `i2cRead` and `i2cWrite` as bare macros.

The folder is named `RefCounter/` so it satisfies the Arduino IDE's rule that a
sketch folder match its `.ino`, while `platformio.ini` points `src_dir` at the
same folder. One copy of the source, two build systems.

### Design notes

**Why none of the Watchy library is used.** Its `Watchy::init()` is the whole
lifecycle — wake, draw one frame, deep sleep — and it ends in `deepSleep()`
without returning, which cannot work for a clock that ticks every second. That
alone forced an own main loop. Reusing just the menu then proved impossible
too: `Watchy::showMenu()` hard-codes six entries, and `showBuzz()`, `setTime()`
and `showSyncNTP()` each finish by redrawing it, so "Show Accelerometer" could
not be dropped without it flashing back for 2.6s after most actions. With the
menu written here anyway, the remaining pieces — a GxEPD2 wrapper, two RTC
chips and an NTP call — were smaller than the dependency they justified.

**Digits are drawn, not typed.** The countdown is seven filled rectangles per
digit rather than a font glyph. That keeps the countdown's exact bounding box
knowable, which is what lets the partial-update window below be hard-coded
safely — the claim now rests on `layoutCount` in `RefSegments.cpp`, which is
host-tested against its own 199 ceiling, rather than on the countdown always
being exactly two digits — and it drops the 14KB DSEG7 font from the build.
The segment geometry is four numbers (`SegStyle`) per size, instantiated in
`RefDisplay.cpp`, so resizing the digits is a one-line change.

**Refresh strategy.** Three tiers, cheapest first:

| Call | Area | Cost | Used for |
| --- | --- | --- | --- |
| `renderDigits()` | 200x124 window | ~280ms | every second of a running clock |
| `render(v, false)` | whole screen, partial | ~400ms | state changes (start, expire, wake) |
| `render(v, true)` | whole screen, full | ~2.6s | returning to Ready |

`renderDigits()` repaints only the countdown into the framebuffer and calls
GxEPD2's `displayWindow()`, so only 62% of the panel is driven — the header,
footer and battery are left standing. The window widened from 176px to the
full 200px so it always covers the skinny hundreds bar; the extra cost every
second is the price of never leaving a stale leading `1` behind when a clock
crosses back under 100.

Partial refreshes leave faint ghosting, and only a full refresh clears it. Both
routes back to Ready arrange for one, but neither pays for it up front:

- **A clock expires.** `00` holds for three seconds, then the return to Ready
  is itself the full refresh. One redraw does both jobs, and by then play has
  stopped.
- **You clear it by hand.** The return is a *partial* refresh, so the button
  responds in ~400ms rather than freezing for 2.6s. The full refresh is queued
  for `SCREEN_TIDY_DELAY_MS` later and only fires once nothing is happening;
  starting a clock cancels it.

Either way the 2.6s stall never lands mid-down.

**Power.** The CPU drops to 80MHz, and the watch light-sleeps between button
polls whenever no clock is running, waking on a button GPIO with a 250ms timer
as a backstop. Low power mode is a real deep sleep with an `ext1` wake on the
top-left button. Set `LIGHT_SLEEP_WHEN_IDLE` to `false` in `settings.h` to fall
back to plain polling if your board misbehaves.

**Accuracy.** The countdown is derived from `esp_timer_get_time()` against the
main crystal. Light sleep during e-paper busy-waits is accounted for using the
calibrated RTC slow clock, which drifts a fraction of a percent — on the order
of a tenth of a second across a 40 second count.

## Configuring

Everything lives in [`RefCounter/settings.h`](RefCounter/settings.h). The ones
you are most likely to touch:

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
static const bool     DARK_MODE           = true;  // true = white on black
static const bool     CLOCK_24_HOUR       = false;
static const char     DEFAULT_TIME_ZONE[] = "Eastern"; // until set in the menu
static const bool     DEFAULT_DST_AUTO    = true; // apply the US rule by date
static const uint32_t NTP_RESYNC_HOURS    = 0;    // 0 = only sync by hand
```

`DEFAULT_SPORT` and the five `CUSTOM_*` values are likewise only a starting
point. The clock lengths themselves moved to the on-watch menu — `Sport` picks
among the six fixed presets, and `Edit Custom` changes the Custom slot's
numbers on the watch, with both choices kept in NVS across a reflash. Editing
these constants and reflashing only changes what a watch starts on before
anyone has touched its menu.

`DEFAULT_TIME_ZONE` and `DEFAULT_DST_AUTO` are only defaults — the menu's
**TZ** and **DST** entries override them and the choice is kept in NVS across
reflashes.

`DARK_MODE` flips the whole theme, panel border included. The stock Watchy
`7_SEG` face ships with its own `DARKMODE true`, which is why it is white on
black; there is no inversion in the display driver itself.

Clock values are limited to 1–199. The display draws two full-size digits
plus, for 100 and up, a skinny leading `1` — just the two vertical bars of a
`1`, one segment thick — to their left; the two full digits shift right to
make room. There is no room for a fourth glyph, which is why 199 is the
ceiling.

The timing values are no longer reflash-only: they sit behind the on-watch
`Sport` and `Edit Custom` entries, which need a deliberate hold from the Ready
screen to reach and time out on their own if left open, the same protection
against a fumbled button that the rest of the menu has. The six fixed presets
cannot be edited at all — only the Custom slot can, and only from the watch.

If the buttons on your unit sit in different positions than the diagram above,
swap the four role defines at the bottom of
[`RefCounter/board.h`](RefCounter/board.h) rather than editing the sketch.

## Installing

### Option A — PlatformIO (recommended)

Requires [PlatformIO Core](https://platformio.org/install/cli) or the VS Code
extension.

Nothing to check out first — the three libraries are pulled from the registry
on the first build. Plug the watch in, then build and flash. Pick the env for your revision —
`watchy_v2` (the default), `watchy_v15`, `watchy_v10`, or `watchy_v3`:

```bash
pio run -e watchy_v2 -t upload
```

The first build downloads about 1.5GB of ESP32 toolchain and takes a few
minutes; later builds take under a minute.

### Option B — Arduino IDE

1. **Board support.** In *Preferences → Additional Board Manager URLs* add:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   then install **esp32** (2.0.5 or newer) from *Tools → Board → Boards Manager*.

2. **Libraries.** *Tools → Manage Libraries*, then install **GxEPD2**,
   **Adafruit GFX Library** and **WiFiManager** (tzapu). Accept the dependency
   prompts. Do *not* install the Watchy library; this sketch does not use it.

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

   V3 needs nothing: selecting an S3 board defines `ARDUINO_ESP32S3_DEV` for
   you. Under PlatformIO this is handled by `build_flags` and no edit is needed.

   Also set *Tools → Partition Scheme* to **Huge APP**, or the WiFi stack will
   not fit.

4. **Open and upload.** Open `RefCounter/RefCounter.ino`, select the serial
   port under *Tools → Port*, and hit Upload.

### If upload fails

The watch must be awake to enumerate over USB. If it is in low power mode, hold
the top-left button to wake it first.

On a **V3**, hold the **top-right (UP)** button while plugging in to force the
bootloader — on that revision UP is GPIO 0, the ESP32-S3 strapping pin. The
ESP32 revisions have no button on GPIO 0 and rely on the USB serial chip's
auto-reset instead, so there is no button to hold; if auto-reset is not
working, it is a cable or port problem rather than a timing one.

## Testing

`RefZone.cpp`, `RefSegments.cpp` and `RefSport.cpp` are pure arithmetic and
compile on a host, so the daylight saving rule, the digit layout and the
preset table are all checked off the watch — this is exactly the kind of bug
(a changeover date, a digit running off the panel, a clamp) that is expensive
to find on hardware and cheap to catch here.

```bash
./tests/run.sh
```

`tests/tz_test.cpp` walks the exact transition instants for 2024, 2025, 2026,
2027 and 2030, checks January and July in all eleven zones, checks that
**DST: Off** pins every zone to standard time, and round-trips 1,460 local
times through local → UTC → local. `tests/tz_edges.cpp` prints what Set Time
does inside the two transition hours. Both build the shipped `RefZone.cpp`
against a stub `Preferences` that reports nothing stored, so the `settings.h`
defaults apply.

`tests/segments_test.cpp` pins `layoutCount`'s numbers for the two styles the
panel actually uses — the two-digit and three-digit layouts, the exact pixel
`x` positions, the 100/99 boundary where the hundreds bar appears and
disappears, and clamping above 199 — then checks, exhaustively and for both
the big (RUNNING/EXPIRED) and small (Ready) styles, that no digit runs off
either edge of the panel for every value 0 through 199. SMALL is not a
theoretical case: the Ready screen draws both of the active preset's clocks
in it, and Lacrosse, Base NCAA and Custom all reach past 100. It checks
`layoutRows` the same way, including the invariant that the upper, middle
and lower segment rows abut with no gap or overlap.

`tests/sport_test.cpp` checks the shipped preset table against the numbers in
*Buzz pattern* above, that `Custom` is always the last entry, that an out-of-range index
falls back to the first preset rather than reading off the end of the table,
and that every name and description fits the menu row and picker footer they
render into. It also exercises persistence itself: the stub `Preferences`
gained a switch (`PreferencesStub::enable`) so a test can turn storage on,
which the time zone tests deliberately never do. With it on, `sport_test`
confirms the selected sport and the edited Custom fields both survive a
simulated reboot (a fresh `RefSport::begin()` call), and that a garbage index
left in NVS clamps back to the first preset rather than being trusted.

Nothing else here has automated tests, and none of this has run on hardware —
see below.

## Known limitations

- **None of this has been run on a watch.** It builds clean for all four
  revisions and the time zone, digit-layout and sport-preset logic are tested
  on a host, but no part of it has been exercised on real hardware. The least
  proven pieces are `RefRtc`'s BCD decoding and chip probing,
  `RefPanel::begin()` (GxEPD2's stock init rather than the reference
  firmware's tuned one), light sleep, and whether the four buttons are
  physically where `board.h` says they are.
- Sport presets add their own untested territory: whether the leading `1`
  actually clears from the panel when a clock crosses 100 → 99 (the widened
  refresh window is the fix; only a real panel confirms it), whether nine
  menu rows scroll legibly with the highlight landing on the right row, and
  whether the Custom editor's five rows fit without the descenders clipping.
  NVS is assumed to survive a reflash for the `refsport` namespace the way it
  reportedly does for `refzone`, but that has never been confirmed either.
- Longer presets mean longer runs of partial refreshes: a 40s Football clock
  was already 39 consecutive `displayWindow` ticks before the deferred full
  refresh, but a 120s Lacrosse clock is 119 and Custom at its 199 ceiling is
  198. The full refresh on the way back to Ready still runs and still clears
  it, so nothing is broken, but how much ghosting a run that long leaves on a
  real panel in between is untested like everything else here.
- Buttons are not sampled during a buzz or a screen refresh. A hold that starts
  and ends inside one of those windows is missed; a hold you keep held is
  always caught, up to ~400ms late.
- Continuous running is the worst case for battery. Expect several hours of
  active use on a full charge; use low power mode between games.
- Bringing up WiFi for an NTP sync blocks for several seconds, and buttons are
  ignored for that whole time. It only happens after 60s untouched, and never
  during a countdown, but it is a real pause if you catch it.
- Only US time zones are listed. Elsewhere, pick whichever standard offset
  matches and turn **DST** off, or add a row to the table in `RefZone.cpp`.
- The daylight saving rule is the current US one, hard-coded. If Congress makes
  daylight saving permanent, this needs a firmware change.
- Adding WiFi grew the binary from 402KB to 1.13MB and RAM use from 29KB to
  55KB. Both are comfortably within a 4MB / 320KB device.
