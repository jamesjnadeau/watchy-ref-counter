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

A purpose-built play clock for officiating football, running on a
[Watchy](https://watchy.sqfmi.com) e-paper watch. Two countdowns, big
7-segment digits, and a buzz pattern you can follow without looking at your
wrist.

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
| Top right | Start, or reset, the **long** clock (40s) | 0.5s |
| Bottom right | Start, or reset, the **short** clock (25s) | 0.5s |
| Top left | Enter low power mode; hold again to wake | 1.0s |
| Bottom left | Clear the clock and return to Ready | 0.5s |

**A short tap does nothing.** Every action needs a deliberate hold, so a button
brushed against a sleeve mid-game cannot reset the play clock. A short
confirmation buzz fires the instant a hold registers, so you know the clock
started without looking down.

Holding a timer button while that clock is already running restarts it from
its full value.

## Buzz pattern

Counting a 40 second clock down, with the stock settings:

| Remaining | Buzz |
| --- | --- |
| 10s | 1 short buzz (150ms) — early warning |
| 5s, 4s, 3s, 2s, 1s | 1 short buzz on each second mark |
| 0s | 1 long buzz (1000ms) |

All five numbers — the warning mark, how many buzzes it fires, which second
the per-second countdown starts from, and both buzz lengths — are in
`settings.h`.

Buzzes are deliberately blocking, and fire *before* the screen redraws: the
buzz is the cue you actually react to, and an e-paper refresh takes most of a
second. This costs no accuracy, because remaining time is always recomputed
from the timestamp the clock started at rather than accumulated tick by tick.

## Screens

**Ready** — shown at boot and on wake. Both configured clocks stacked to line
up with the two right-hand buttons that start them.

**Running** — the current clock, large, updating every second. The header shows
which clock is running, plus a battery gauge.

**Time expired** — `00` holds for `EXPIRED_HOLD_MS` (3s), long enough to
register the delay of game, then the watch drops back to Ready on its own.
Starting a new clock during that window cancels the return.

**Sleeping** — low power mode.

## Repo structure

```
watchy-ref-counter/
├── README.md          this file
├── platformio.ini     build config; one env per Watchy revision
└── RefCounter/        the sketch (also a valid Arduino IDE sketch folder)
    ├── RefCounter.ino main loop and the IDLE/RUNNING/EXPIRED/SLEEPING states
    ├── settings.h     every tunable number, and nothing else
    ├── board.h        per-revision differences: button polarity and roles
    ├── Buttons.h/.cpp debounced reads with one-shot hold detection
    ├── Buzzer.h/.cpp  vibration motor
    └── RefDisplay.h/.cpp  screen layout and e-paper refresh strategy
```

The folder is named `RefCounter/` so it satisfies the Arduino IDE's rule that a
sketch folder match its `.ino`, while `platformio.ini` points `src_dir` at the
same folder. One copy of the source, two build systems.

### Design notes

**The sketch does not subclass `Watchy`.** The base class is built around
"wake, draw one frame, deep sleep" — `Watchy::init()` ends in `deepSleep()` and
never returns. A clock that ticks every second has to stay awake and own its
own loop, so this sketch borrows the library's display driver and pin map and
drives them directly. It reuses `Watchy::display` rather than constructing a
second 5KB framebuffer.

**Digits are drawn, not typed.** The countdown is seven filled rectangles per
digit rather than a font glyph. That makes the exact bounding box of the
countdown known at compile time, which is what lets the partial-update window
below be hard-coded safely — and it drops the 14KB DSEG7 font from the build.
The segment geometry is four numbers (`SegStyle`) in `RefDisplay.cpp`, so
resizing the digits is a one-line change.

**Refresh strategy.** Three tiers, cheapest first:

| Call | Area | Cost | Used for |
| --- | --- | --- | --- |
| `renderDigits()` | 176x124 window | ~250ms | every second of a running clock |
| `render(v, false)` | whole screen, partial | ~400ms | state changes (start, expire, wake) |
| `render(v, true)` | whole screen, full | ~2.6s | returning to Ready |

`renderDigits()` repaints only the countdown into the framebuffer and calls
GxEPD2's `displayWindow()`, so only 54% of the panel is driven — the header,
footer and battery are left standing.

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
static const uint16_t TIMER_LONG_SECONDS  = 40;   // top-right button
static const uint16_t TIMER_SHORT_SECONDS = 25;   // bottom-right button
static const uint32_t TIMER_HOLD_MS       = 500;  // hold to start / reset
static const uint32_t SLEEP_HOLD_MS       = 1000; // hold for low power mode
static const uint32_t BUZZ_EXPIRE_MS      = 1000; // buzz at zero
static const uint16_t WARNING_AT_SECONDS  = 10;   // early warning mark
static const uint8_t  WARNING_BUZZ_COUNT  = 1;    // buzzes at that mark
static const uint16_t FINAL_COUNTDOWN_FROM = 5;   // buzz each of the last N
static const bool     DARK_MODE           = false; // false = black on white
```

`DARK_MODE` flips the whole theme, panel border included. The stock Watchy
`7_SEG` face ships with its own `DARKMODE true`, which is why it is white on
black; there is no inversion in the display driver itself.

Clock values are limited to 1–99, because the display shows two digits.

Change a value, reflash. There is no on-device settings menu — one fewer thing
to fat-finger during a game.

If the buttons on your unit sit in different positions than the diagram above,
swap the four role defines at the bottom of
[`RefCounter/board.h`](RefCounter/board.h) rather than editing the sketch.

## Installing

### Option A — PlatformIO (recommended)

Requires [PlatformIO Core](https://platformio.org/install/cli) or the VS Code
extension.

This repo expects the Watchy library checked out beside it:

```bash
git clone https://github.com/sqfmi/Watchy.git ../Watchy
```

To build against upstream instead, change `lib_deps` in `platformio.ini` to
`https://github.com/sqfmi/Watchy.git`.

Plug the watch in, then build and flash. Pick the env for your revision —
`watchy_v2` (the default), `watchy_v15`, `watchy_v10`, or `watchy_v3`:

```bash
pio run -e watchy_v2 -t upload
```

Everything else — the ESP32 toolchain, GxEPD2, Adafruit GFX — is pulled in
automatically on the first build. The first one downloads about 1.5GB of
toolchain and takes a few minutes; later builds take under a minute.

One dependency is pinned on purpose. Watchy's `library.json` asks for
`orbitalair/Rtc_Pcf8563.git#master`, and that master has since grown a
`Rtc_Pcf8563(WireBase&)` constructor referencing a type the ESP32 core does not
have, so it no longer compiles. `platformio.ini` pins the registry build
instead. Nothing here touches the RTC — it only has to compile because the
Watchy library links as a whole.

### Option B — Arduino IDE

1. **Board support.** In *Preferences → Additional Board Manager URLs* add:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   then install **esp32** (2.0.5 or newer) from *Tools → Board → Boards Manager*.

2. **Library.** *Tools → Manage Libraries*, search for **Watchy**, install it
   along with its dependencies when prompted. This also pulls in GxEPD2 and
   Adafruit GFX.

3. **Board target.** *Tools → Board → ESP32 Arduino → **Watchy***, then
   *Tools → Revision* → **Watchy v2.0**, **v1.5** or **v1.0** to match your
   hardware. Do not use a generic ESP32 Dev Module: the board variant is what
   supplies the pin map, and the revision menu is what selects `UP_BTN_PIN` and
   `BATT_ADC_PIN`, which differ between revisions.

   For a **Watchy V3**, the ESP32-S3 board: pick *ESP32S3 Dev Module* instead.
   V3 is not in the core's board list, but it does not need to be — for V3 the
   Watchy library's own `config.h` defines the whole pin map directly.

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

## Known limitations

- Buttons are not sampled during a buzz or a screen refresh. A hold that starts
  and ends inside one of those windows is missed; a hold you keep held is
  always caught, up to ~400ms late.
- The watch does not keep time of day. This is a play clock, not a watch face —
  the RTC and accelerometer are left untouched, which is part of why it lasts a
  game.
- Continuous running is the worst case for battery. Expect several hours of
  active use on a full charge; use low power mode between games.
