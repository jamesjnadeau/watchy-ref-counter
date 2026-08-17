// ---------------------------------------------------------------------------
// Watchy Ref Counter - a purpose built play clock for officiating football.
//
//   top right    hold -> start / reset the long clock  (40s by default)
//   bottom right hold -> start / reset the short clock (25s by default)
//   top left     hold -> low power mode, hold again to wake
//
// A short tap never does anything. Every tunable number lives in settings.h.
//
// This sketch deliberately does not use the Watchy base class. Watchy is built
// around "wake, draw one frame, deep sleep", and Watchy::init() ends in
// deepSleep() and never returns. A clock that ticks every second needs to stay
// awake and own its own loop, so we borrow the library's display driver, pin
// map and font, and drive them directly.
// ---------------------------------------------------------------------------

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "board.h"
#include "settings.h"

static AppState state       = STATE_IDLE;
static uint16_t durationSec = TIMER_LONG_SECONDS;
static uint16_t shownSec    = 0;

// Wall clock origin for the running countdown. Remaining time is always
// recomputed from this, never accumulated, so a blocking buzz or a slow screen
// refresh costs nothing in accuracy.
static int64_t startUs = 0;

// When the EXPIRED screen should give way back to the ready screen. Only
// meaningful while state == STATE_EXPIRED.
static uint32_t returnToIdleAt = 0;

static View currentView() {
  View v;
  v.state       = state;
  v.secondsLeft = shownSec;
  v.durationSec = durationSec;
  return v;
}

static void enterIdle(bool full) {
  state = STATE_IDLE;
  RefDisplay::render(currentView(), full);
}

static void startTimer(uint16_t seconds) {
  // Take the timestamp first: the clock starts when the hold registered, not
  // after the confirmation buzz and the screen refresh have finished.
  startUs     = esp_timer_get_time();
  durationSec = seconds;
  shownSec    = seconds;
  state       = STATE_RUNNING;

  Buzzer::pulse(BUZZ_CONFIRM_MS);
  RefDisplay::render(currentView(), false);
}

// Buzz for whatever mark the clock just landed on. Ordering matters: the
// per-second countdown takes precedence over the early warning, so setting
// WARNING_AT_SECONDS inside FINAL_COUNTDOWN_FROM silently disables it.
static void buzzForMark(uint16_t secondsLeft) {
  if (secondsLeft == 0) {
    Buzzer::pulse(BUZZ_EXPIRE_MS);
  } else if (FINAL_COUNTDOWN_FROM > 0 && secondsLeft <= FINAL_COUNTDOWN_FROM) {
    Buzzer::pulse(BUZZ_SHORT_MS);
  } else if (WARNING_BUZZ_COUNT > 0 && secondsLeft == WARNING_AT_SECONDS) {
    Buzzer::pulse(WARNING_BUZZ_COUNT, BUZZ_SHORT_MS, BUZZ_GAP_MS);
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
#ifdef ARDUINO_ESP32S3_DEV
  rtc_gpio_set_direction((gpio_num_t)BTN_SLEEP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)BTN_SLEEP_PIN);
#else
  // Park the unused GPIOs as inputs so they stop leaking current while asleep.
  // Mask and technique lifted from Watchy::deepSleep() in the Watchy library.
  const uint64_t ignore = 0b11110001000000110000100111000010;
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if ((ignore >> i) & 0b1) {
      continue;
    }
    pinMode(i, INPUT);
  }
#endif
  // Idle light sleep and the display driver's busy-wait both leave GPIO wake
  // armed, and GPIO is not a valid deep sleep source on the ESP32. Clear
  // everything first so ext1 is the only way back up.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(BIT64(BTN_SLEEP_PIN), BTN_EXT1_WAKE_MODE);
  esp_deep_sleep_start();
}

static void enterSleep() {
  Buzzer::off();
  state = STATE_SLEEPING;

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

  gpio_wakeup_enable((gpio_num_t)BTN_LONG_TIMER_PIN, BTN_LIGHT_SLEEP_WAKE_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_SHORT_TIMER_PIN, BTN_LIGHT_SLEEP_WAKE_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_SLEEP_PIN, BTN_LIGHT_SLEEP_WAKE_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  // Timer wake as a backstop: if a GPIO wake is ever missed, the cost is one
  // poll interval rather than a watch that ignores its buttons.
  esp_sleep_enable_timer_wakeup((uint64_t)IDLE_SLEEP_MS * 1000ULL);

  esp_light_sleep_start();

  gpio_wakeup_disable((gpio_num_t)BTN_LONG_TIMER_PIN);
  gpio_wakeup_disable((gpio_num_t)BTN_SHORT_TIMER_PIN);
  gpio_wakeup_disable((gpio_num_t)BTN_SLEEP_PIN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
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
  if (busy) {
    delay(BUTTON_POLL_MS);
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
  RefDisplay::begin();

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
    startTimer(TIMER_LONG_SECONDS);
    return;
  }
  if (Buttons::heldFor(Buttons::SHORT_TIMER, TIMER_HOLD_MS)) {
    startTimer(TIMER_SHORT_SECONDS);
    return;
  }

  if (state == STATE_RUNNING) {
    tickRunning();
    delay(BUTTON_POLL_MS);
  } else {
    idleTick();
  }
}
