#ifndef REF_SETTINGS_H
#define REF_SETTINGS_H

// ---------------------------------------------------------------------------
// Every tunable value for the ref play clock lives here. Change a value,
// reflash, done. Nothing else in the sketch hard-codes these numbers.
// ---------------------------------------------------------------------------

#include <stdint.h>

static const char REF_COUNTER_VERSION[] = "1.1";

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

// How long `00` stays up after a clock expires before the watch drops back to
// the ready screen. That return is a full refresh, so it doubles as the ghost
// clear after a run of partial updates - deferred to here deliberately, so the
// 2.6s stall never lands in the middle of live play.
//
// Starting a new clock during this window cancels the return.
static const uint32_t EXPIRED_HOLD_MS = 3000;

// Clearing with the bottom-left button redraws instantly with a partial
// refresh so it feels immediate. That leaves ghosting behind, so a full
// refresh follows once this long has passed with nothing happening. Starting a
// clock cancels it, so the tidy-up never interrupts play.
static const uint32_t SCREEN_TIDY_DELAY_MS = 3000;

// --- Clock, menu and NTP ---------------------------------------------------
// The time shows at the top left of every screen. It comes from the watch's
// real time clock chip, which keeps running through low power mode and through
// a reflash.
static const bool CLOCK_24_HOUR = false;

// Time zone. This is only the starting point: the menu's "TZ" entry picks a
// zone on the watch and stores it, and from then on the stored value wins.
// Must match one of the names in RefZone.cpp, which are:
//
//   Eastern   Central   Mountain  Arizona   Pacific   Alaska
//   Aleutian  Hawaii    Samoa     Atlantic  Chamorro
//
// Arizona, Hawaii, Samoa, Atlantic (Puerto Rico and the USVI) and Chamorro
// (Guam and the Northern Marianas) do not observe daylight saving.
static const char DEFAULT_TIME_ZONE[] = "Eastern";

// Starting position of the menu's "DST" switch, likewise overridden once the
// switch has been touched on the watch. On means the US rule is applied when
// the date calls for it -- forward on the second Sunday in March, back on the
// first Sunday in November -- not that the clock is shifted year round.
static const bool DEFAULT_DST_AUTO = true;

static const char NTP_SERVER[] = "pool.ntp.org";

// How long to wait for the time server to answer.
static const uint32_t NTP_TIMEOUT_MS = 10000;

// The access point the "Setup WiFi" screen raises, and how long it stays up.
static const char     WIFI_AP_NAME[]     = "Watchy Ref Counter";
static const uint16_t WIFI_AP_TIMEOUT_S  = 120;

// The RTC drifts a little every day - roughly a minute a month on the PCF8563
// fitted to V2.0. Re-syncing over WiFi keeps it well under a second, but a
// sync blocks
// for several seconds while the radio comes up, so it is only worth doing
// when nobody is around to notice. An automatic resync therefore waits for
// both of these to be true: the watch has sat untouched (no button press, no
// sleep entry) for NTP_RESYNC_HOURS, and at least NTP_MIN_SYNC_INTERVAL_HOURS
// have passed since the last attempt, so a run of idle moments cannot trigger
// back-to-back radio activity. The deep-sleep timer is armed against this
// same schedule, so the watch wakes itself for the resync rather than relying
// on the next button press. Set NTP_RESYNC_HOURS to 0 to only ever sync by
// hand from the menu.
static const uint32_t NTP_RESYNC_HOURS = 3;
static const uint32_t NTP_MIN_SYNC_INTERVAL_HOURS = 24;

// --- Bluetooth time sync ---------------------------------------------------
// The other way to set the clock: a phone, over Bluetooth Low Energy, with no
// WiFi involved. The watch advertises the standard Current Time Service while
// the menu's "Sync BT" screen is open and the phone writes the time into it --
// Gadgetbridge does that on its own, and nRF Connect can do it by hand. See
// the README for what to press on the phone.
//
// Set false to take it off the watch: the menu row goes away, the automatic
// fallback below never fires, and the radio is never powered up. The BLE
// library is still linked into the build either way, so that saves battery
// rather than flash. Left on it costs nothing while no sync window is open --
// the radio only comes up inside one.
static const bool BT_TIME_SYNC = true;

// The name the watch advertises under -- what you pick out of the list on the
// phone. Keep it short: it rides in the 31 byte scan response, and the BLE
// library warns that a name past 29 characters overruns that packet.
static const char BT_DEVICE_NAME[] = "Ref Counter";

// How long the "Sync BT" screen waits for a phone before giving up. BACK
// cancels it sooner.
static const uint32_t BT_SYNC_TIMEOUT_MS = 60000;

// Automatic sync fallback. When an automatic sync falls due (the schedule
// above) and NTP is not available -- no WiFi credentials saved, or the
// connection failed -- the watch can advertise for this many seconds in the
// hope that a companion app writes the time. 0 leaves automatic sync to NTP
// alone, which is the default: it is worth having only if a phone app is set
// up to sync time to this watch by itself, and advertising to nobody costs
// battery. The manual "Sync BT" screen works either way.
static const uint32_t BT_AUTO_SYNC_SECONDS = 0;

// Leave the menu and return to the ready screen after this long with no button
// presses, so a menu opened by accident cannot strand you mid-game.
static const uint32_t MENU_TIMEOUT_MS = 15000;

// The menu's "Show Accelerometer" entry needs the BMA423 powered up. Nothing
// else here uses it, so set false to save that current if you never look.
static const bool ENABLE_ACCELEROMETER = false;

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
