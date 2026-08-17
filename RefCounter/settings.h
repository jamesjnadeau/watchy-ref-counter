#ifndef REF_SETTINGS_H
#define REF_SETTINGS_H

// ---------------------------------------------------------------------------
// Every tunable value for the ref play clock lives here. Change a value,
// reflash, done. Nothing else in the sketch hard-codes these numbers.
// ---------------------------------------------------------------------------

#include <stdint.h>

// --- Countdown lengths -----------------------------------------------------
// Top-right button runs the long clock, bottom-right runs the short clock.
// Valid range is 1..99 (the display shows two digits).
static const uint16_t TIMER_LONG_SECONDS  = 40;
static const uint16_t TIMER_SHORT_SECONDS = 25;

// --- Button hold thresholds ------------------------------------------------
// How long a right-hand button must be held before it starts (or restarts)
// its clock. A short tap is deliberately ignored so a bumped button during a
// game cannot reset the play clock.
static const uint32_t TIMER_HOLD_MS = 500;

// How long the top-left button must be held to drop into low power mode.
// Kept longer than TIMER_HOLD_MS so the two are hard to confuse.
static const uint32_t SLEEP_HOLD_MS = 1000;

// --- Buzzer ----------------------------------------------------------------
// Single long buzz when the clock reaches zero.
static const uint32_t BUZZ_EXPIRE_MS = 1000;

// Early warning: when this many seconds remain, buzz WARNING_BUZZ_COUNT times.
// Set WARNING_BUZZ_COUNT to 0 to disable the warning entirely.
// Note: if WARNING_AT_SECONDS is set at or below FINAL_COUNTDOWN_FROM, the
// per-second countdown buzz wins and the warning never fires.
static const uint16_t WARNING_AT_SECONDS = 10;
static const uint8_t  WARNING_BUZZ_COUNT = 1;

// Tick buzz on each of the last N second marks (5, 4, 3, 2, 1).
// Set to 0 to disable the per-second countdown.
static const uint16_t FINAL_COUNTDOWN_FROM = 5;

// Length of a short buzz, and the silent gap between repeats.
static const uint32_t BUZZ_SHORT_MS = 150;
static const uint32_t BUZZ_GAP_MS   = 120;

// Tiny confirmation buzz the instant a hold registers, so you know the clock
// started without looking at the watch. Set to 0 to disable.
static const uint32_t BUZZ_CONFIRM_MS = 60;

// --- Display ---------------------------------------------------------------
// false = black digits on a white background. true = white on black, the way
// the stock Watchy 7_SEG face ships. This drives the panel border too.
static const bool DARK_MODE = true;

// After a clock finishes, the sketch waits this long and then does one slow
// full refresh to clear e-paper ghosting. It is deferred so it never lands in
// the middle of live play.
static const uint32_t DEGHOST_DELAY_MS = 3000;

// --- Power -----------------------------------------------------------------
// CPU clock while awake. 80 MHz roughly halves current draw versus the 240 MHz
// default and is plenty for driving the e-paper. Set to 0 to leave it alone.
static const uint32_t CPU_FREQ_MHZ = 80;

// Light sleep between button polls while no clock is running. This is the
// single biggest battery win. If your board misbehaves, set to false to fall
// back to a plain polling delay.
static const bool LIGHT_SLEEP_WHEN_IDLE = true;

// Safety-net wakeup while light sleeping, in case GPIO wake does not fire.
static const uint32_t IDLE_SLEEP_MS = 250;

// --- Buttons ---------------------------------------------------------------
static const uint32_t BUTTON_POLL_MS     = 20;
static const uint32_t BUTTON_DEBOUNCE_MS = 30;

// Give up waiting for a stuck button after this long, so the watch can never
// hang on the way into low power mode.
static const uint32_t BUTTON_RELEASE_TIMEOUT_MS = 5000;

#endif // REF_SETTINGS_H
