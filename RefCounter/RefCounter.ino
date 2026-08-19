// ---------------------------------------------------------------------------
// Watchy Ref Counter - a purpose built play clock for officiating football.
//
//   top right    hold -> start / reset the long clock  (40s by default)
//   bottom right hold -> start / reset the short clock (25s by default)
//   bottom left  hold -> on the ready screen, open the settings menu;
//                          otherwise clear the clock and go back to it
//   top left     hold -> low power mode, hold again to wake
//
// A short tap never does anything. Every tunable number lives in settings.h.
//
// This project is self-contained: it does not link against the Watchy library.
// The panel is driven through GxEPD2, the RTC chips over Wire, and NTP through
// the ESP32 core's SNTP client. Where the behaviour follows the reference
// firmware (sqfmi/Watchy) the comments say so, but none of its code is used.
// ---------------------------------------------------------------------------

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "RefMenu.h"
#include "RefClock.h"
#include "RefSport.h"
#include "RefSyncSchedule.h"
#include "RefWifi.h"
#include "board.h"
#include "settings.h"

static RefClock refClock;

static AppState state       = STATE_IDLE;
static uint16_t durationSec = 0;
static uint16_t shownSec    = 0;

// Wall clock origin for the running countdown. Remaining time is always
// recomputed from this, never accumulated, so a blocking buzz or a slow screen
// refresh costs nothing in accuracy.
static int64_t startUs = 0;

// When the EXPIRED screen should give way back to the ready screen. Only
// meaningful while state == STATE_EXPIRED.
static uint32_t returnToIdleAt = 0;

// A manual clear redraws with a fast partial refresh so it feels immediate,
// then queues the slow ghost-clearing refresh for once things go quiet.
static bool     tidyPending = false;
static uint32_t tidyAt      = 0;

// Cached wall clock. The RTC is read over I2C at most once a second rather
// than every loop, and the header is only repainted when the minute changes.
static uint8_t  clockHour   = 0;
static uint8_t  clockMinute = 0;
static bool     clockValid  = false;
static uint32_t clockReadAt = 0;

static View currentView() {
  View v;
  v.state       = state;
  v.secondsLeft = shownSec;
  v.durationSec = durationSec;
  v.hour        = clockHour;
  v.minute      = clockMinute;
  v.clockValid  = clockValid;
  return v;
}

// Re-read the RTC at most once a second. Returns true when the displayed
// minute changed, which is the only time the header needs repainting.
static bool refreshClock() {
  const uint32_t now = millis();
  if (clockValid && (now - clockReadAt) < 1000) {
    return false;
  }
  clockReadAt = now;

  uint8_t h = clockHour, m = clockMinute;
  const bool ok = refClock.read(h, m);
  const bool changed = (ok != clockValid) || (ok && (h != clockHour || m != clockMinute));
  clockValid  = ok;
  clockHour   = h;
  clockMinute = m;
  return changed;
}

static void enterIdle(bool full) {
  state       = STATE_IDLE;
  tidyPending = false;
  refClock.noteActivity();
  RefDisplay::render(currentView(), full);
}

static void startTimer(uint16_t seconds) {
  // Take the timestamp first: the clock starts when the hold registered, not
  // after the confirmation buzz and the screen refresh have finished.
  startUs     = esp_timer_get_time();
  durationSec = seconds;
  shownSec    = seconds;
  state       = STATE_RUNNING;
  tidyPending = false;
  refClock.noteActivity();

  Buzzer::pulse(BUZZ_CONFIRM_MS);
  RefDisplay::render(currentView(), false);
}

// Abandon whatever is on the clock and go back to the ready screen. Uses a
// partial refresh so it responds straight away; the ghosting that leaves is
// cleaned up by the deferred full refresh queued here.
static void clearToIdle() {
  Buzzer::pulse(BUZZ_CONFIRM_MS);
  enterIdle(false);
  tidyPending = true;
  tidyAt      = millis() + SCREEN_TIDY_DELAY_MS;
}

// The bottom-left button does one of two things. On the ready screen there is
// no clock to clear, so it opens the reference settings menu instead; anywhere
// else it clears back to the ready screen.
static void bottomLeftHold() {
  if (state != STATE_IDLE) {
    clearToIdle();
    return;
  }
  Buzzer::pulse(BUZZ_CONFIRM_MS);
  RefMenu::open(refClock);
  refreshClock(); // the menu may have set the time or synced it
  enterIdle(true);
}

// Buzz for whatever mark the clock just landed on, using the active preset's
// numbers. Ordering matters: the per-second countdown takes precedence over
// both warnings, so a mark set inside finalCountdownFrom is silently swallowed.
static void buzzForMark(uint16_t secondsLeft) {
  const RefSport::Preset p = RefSport::active();

  if (secondsLeft == 0) {
    Buzzer::pulse(BUZZ_EXPIRE_MS);
  } else if (p.finalCountdownFrom > 0 && secondsLeft <= p.finalCountdownFrom) {
    Buzzer::pulse(BUZZ_SHORT_MS);
  } else if (WARNING_BUZZ_COUNT > 0 && p.warnAtSeconds > 0 &&
             secondsLeft == p.warnAtSeconds) {
    Buzzer::pulse(WARNING_BUZZ_COUNT, BUZZ_SHORT_MS, BUZZ_GAP_MS);
  } else if (WARNING_BUZZ_COUNT_2 > 0 && p.warn2AtSeconds > 0 &&
             secondsLeft == p.warn2AtSeconds) {
    Buzzer::pulse(WARNING_BUZZ_COUNT_2, BUZZ_SHORT_MS, BUZZ_GAP_MS);
  }
}

static void tickRunning() {
  const int64_t elapsedMs = (esp_timer_get_time() - startUs) / 1000;
  const int64_t totalMs   = (int64_t)durationSec * 1000;

  const uint16_t left =
      elapsedMs >= totalMs ? 0 : durationSec - (uint16_t)(elapsedMs / 1000);
  if (left == shownSec) {
    return;
  }
  shownSec = left;

  // Buzz before redrawing. The buzz is the cue an official acts on, and the
  // e-paper refresh would otherwise delay it by most of a second.
  buzzForMark(left);

  if (left == 0) {
    // Expiring rewrites the header and footer too, so this one is whole screen.
    state          = STATE_EXPIRED;
    returnToIdleAt = millis() + EXPIRED_HOLD_MS;
    RefDisplay::render(currentView(), false);
    return;
  }
  // Every other tick only changes the number, so only that window is repainted.
  RefDisplay::renderDigits(currentView());
}

static void deepSleepUntilButton() {
#ifdef ARDUINO_ESP32C6_DEV
  rtc_gpio_set_direction((gpio_num_t)BTN_SLEEP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)BTN_SLEEP_PIN);
#else
  // Stop driving the lines this sketch pulls, so none of them leaks current
  // over the hours the watch may spend asleep. The panel is already
  // hibernated, so releasing its control lines is safe.
  const uint8_t driven[] = {PIN_VIB_MOTOR,   PIN_DISPLAY_CS, PIN_DISPLAY_DC,
                            PIN_DISPLAY_RST, PIN_SPI_SCK,    PIN_SPI_MOSI};
  for (uint8_t pin : driven) {
    pinMode(pin, INPUT);
  }
#endif
  // Idle light sleep and the display driver's busy-wait both leave GPIO wake
  // armed, and GPIO is not a valid deep sleep source on the ESP32. Clear
  // everything first so ext1 is the only way back up.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(BIT64(BTN_SLEEP_PIN), BTN_EXT1_WAKE_MODE);

  // Also arm a timer wake against the resync schedule, so the watch can pull
  // itself out of deep sleep to correct RTC drift rather than waiting for the
  // next button press, which might be hours past due. setup() recognises this
  // wake cause and syncs quietly before going straight back to sleep.
  const uint32_t untilSync = refClock.secondsUntilSyncDue();
  if (untilSync != RefSyncSchedule::NEVER) {
    // Clamp: an already-due sync would otherwise ask for a zero length timer.
    const uint32_t secs = untilSync < 60 ? 60 : untilSync;
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  }

  esp_deep_sleep_start();
}

static void enterSleep() {
  Buzzer::off();
  state = STATE_SLEEPING;

  // The hold that triggered sleep already counted as activity, but sleep
  // entry gets its own stamp too, since it is the moment the watch actually
  // goes quiet and the resync schedule should count its wait from.
  refClock.noteActivity();
  RefDisplay::renderSleeping();
  RefDisplay::hibernate();

  // Otherwise the button we are still holding wakes us straight back up.
  Buttons::waitForRelease();
  deepSleepUntilButton();
}

// Doze between button polls while nothing is counting down. This is where the
// watch spends most of a game, so it is worth the extra care.
static void idleSleep() {
  if (!LIGHT_SLEEP_WHEN_IDLE) {
    delay(BUTTON_POLL_MS);
    return;
  }

  // Buttons owns the pins' interrupt configuration, so it owns the arming too.
  Buttons::armLightSleepWake();
  // Timer wake as a backstop: if a GPIO wake is ever missed, the cost is one
  // poll interval rather than a watch that ignores its buttons.
  esp_sleep_enable_timer_wakeup((uint64_t)IDLE_SLEEP_MS * 1000ULL);

  esp_light_sleep_start();

  Buttons::disarmLightSleepWake();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

static void idleTick() {
  const bool busy = Buttons::anyDown();

  if (state == STATE_EXPIRED) {
    // Hold 00 on screen a moment, then fall back to the ready screen. Waiting
    // for the buttons to be clear keeps the 2.6s full refresh from swallowing
    // a hold that is already under way.
    if (!busy && (int32_t)(millis() - returnToIdleAt) >= 0) {
      enterIdle(true);
    } else {
      delay(BUTTON_POLL_MS);
    }
    return;
  }
  if (tidyPending) {
    if (!busy && (int32_t)(millis() - tidyAt) >= 0) {
      tidyPending = false;
      RefDisplay::render(currentView(), true);
    } else {
      delay(BUTTON_POLL_MS);
    }
    return;
  }
  if (busy) {
    refClock.noteActivity();
    delay(BUTTON_POLL_MS);
    return;
  }

  if (refreshClock()) {
    RefDisplay::renderHeader(currentView());
  }

  // Correct RTC drift, but only once the watch has been left alone for hours:
  // bringing the radio up blocks for several seconds.
  if (refClock.syncDue()) {
    refClock.connectAndSync();
    refClock.noteActivity();
    clockValid = false; // force a re-read at the new time
    if (refreshClock()) {
      RefDisplay::renderHeader(currentView());
    }
    return;
  }

  idleSleep();
}

void setup() {
  if (CPU_FREQ_MHZ > 0) {
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
  }
  Buzzer::begin();
  Buttons::begin();
  RefSport::begin();
  RefWifi::begin();

  // A timer wake means deepSleepUntilButton() pulled the watch out of deep
  // sleep on its own, purely to resync the RTC. The SLEEPING screen is still
  // sitting on the panel from before, so this path must not touch the
  // display at all -- bringing it up would flash the panel awake for a
  // moment nobody asked to see.
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const bool resyncWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
  if (!resyncWake) {
    RefDisplay::begin();
  }
  refClock.begin(cause == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (resyncWake) {
    refClock.connectAndSync();
    // Re-anchor the quiet period, exactly as the awake path does after its
    // own sync. Otherwise the schedule still reads as due the moment this
    // wake ends, and with NTP_MIN_SYNC_INTERVAL_HOURS set to 0 the watch
    // would wake, sync and re-sleep on the timer's one minute floor until the
    // battery was flat. Stamping after the sync cannot suppress that sync.
    refClock.noteActivity();
    // anyDown() reads debounced state from the last poll(), which is still
    // whatever it was before the multi-second sync ran. Poll once now so a
    // press held throughout the sync is actually seen: poll() debounces off
    // the ISR's edge timestamp, not the time of the poll, so a press from
    // seconds ago clears BUTTON_DEBOUNCE_MS on this first call.
    Buttons::poll();
    if (!Buttons::anyDown()) {
      deepSleepUntilButton(); // never returns
    }
    // That same edge timestamp now has to be thrown away. The press was
    // measured from its real moment of contact, seconds back inside the sync,
    // so the main loop would see a hold that had long since passed its
    // threshold and act on it at once -- putting the watch straight back to
    // sleep, or starting a clock nobody asked for. Re-seed the debounce state
    // instead, as RefMenu does after reading the pins behind Buttons' back.
    Buttons::resync();
    // The sleep button is down now that the sync has finished. Rather than
    // trying to queue that press, fall through into a normal start; the
    // watch simply wakes up instead of going back to sleep.
    RefDisplay::begin();
  }

  refreshClock();

  // Full refresh on the way in, whether this is a cold boot or a wake from
  // low power mode, so the panel starts clean.
  enterIdle(true);
}

void loop() {
  Buttons::poll();

  if (Buttons::heldFor(Buttons::SLEEP, SLEEP_HOLD_MS)) {
    enterSleep();
    return;
  }
  if (Buttons::heldFor(Buttons::LONG_TIMER, TIMER_HOLD_MS)) {
    startTimer(RefSport::active().longSeconds);
    return;
  }
  if (Buttons::heldFor(Buttons::SHORT_TIMER, TIMER_HOLD_MS)) {
    startTimer(RefSport::active().shortSeconds);
    return;
  }
  if (Buttons::heldFor(Buttons::RESET, TIMER_HOLD_MS)) {
    bottomLeftHold();
    return;
  }

  if (state == STATE_RUNNING) {
    tickRunning();
    delay(BUTTON_POLL_MS);
  } else {
    idleTick();
  }
}
