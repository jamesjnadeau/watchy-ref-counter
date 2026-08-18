#include "Buttons.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "board.h"
#include "settings.h"

namespace Buttons {
namespace {

struct Button {
  uint8_t pin;

  // Written by the interrupt handler, read by poll() under `mux`.
  volatile bool     edgeLevel; // level the handler saw at the last edge
  volatile int64_t  edgeUs;    // when that edge landed
  volatile uint32_t edgeSeq;   // bumped on every edge, so a bounce that lands
                               // between two polls is not missed

  // poll() owns everything below.
  uint32_t seenSeq;    // last edge poll() has accounted for
  bool     raw;        // last level taken from the handler
  uint32_t rawSinceMs; // when that level settled
  bool     stable;     // debounced level: true == pressed
  uint32_t pressedAt;  // when `stable` last went true
  bool     holdFired;  // hold action already delivered for this press
};

Button buttons[COUNT] = {
    {BTN_LONG_TIMER_PIN, false, 0, 0, 0, false, 0, false, 0, false},
    {BTN_SHORT_TIMER_PIN, false, 0, 0, 0, false, 0, false, 0, false},
    {BTN_SLEEP_PIN, false, 0, 0, 0, false, 0, false, 0, false},
    {BTN_RESET_PIN, false, 0, 0, 0, false, 0, false, 0, false},
};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

bool readPin(uint8_t pin) { return digitalRead(pin) == BTN_PRESSED_LEVEL; }

// Runs at interrupt time, so it may touch nothing but this struct: no display,
// no I2C, no heap, and nothing that lives outside IRAM. Recording the edge is
// all it does; deciding what the edge means is poll()'s job.
void ARDUINO_ISR_ATTR onEdge(void *arg) {
  Button *b    = (Button *)arg;
  b->edgeLevel = (digitalRead(b->pin) == BTN_PRESSED_LEVEL);
  b->edgeUs    = esp_timer_get_time();
  b->edgeSeq++;
}

} // namespace

void resync() {
  const int64_t nowUs = esp_timer_get_time();
  for (uint8_t i = 0; i < COUNT; i++) {
    Button &b = buttons[i];
    const bool level = readPin(b.pin);

    portENTER_CRITICAL(&mux);
    b.edgeLevel = level;
    b.edgeUs    = nowUs;
    b.seenSeq   = b.edgeSeq;
    portEXIT_CRITICAL(&mux);

    b.raw        = level;
    b.stable     = level;
    b.rawSinceMs = (uint32_t)(nowUs / 1000);
    b.pressedAt  = b.rawSinceMs;
    // A button already down -- the one that just woke us, for instance -- must
    // not read as a fresh press.
    b.holdFired = level;
  }
}

void begin() {
  for (uint8_t i = 0; i < COUNT; i++) {
    pinMode(buttons[i].pin, INPUT);
  }
  // Seed first, then arm: an edge arriving mid-seed would otherwise be thrown
  // away by the very code that is meant to catch it.
  resync();
  for (uint8_t i = 0; i < COUNT; i++) {
    attachInterruptArg(buttons[i].pin, onEdge, &buttons[i], CHANGE);
  }
}

void poll() {
  for (uint8_t i = 0; i < COUNT; i++) {
    Button &b = buttons[i];

    // Take the handler's view of the pin, then check it against the pin
    // itself. An edge lost while interrupts were masked, or one that landed
    // during light sleep, would otherwise leave the button stuck at its old
    // level for good. Sampling catches that, at the cost of a timestamp only
    // as good as this poll.
    portENTER_CRITICAL(&mux);
    bool level = b.edgeLevel;
    if (readPin(b.pin) != level) {
      level       = !level;
      b.edgeLevel = level;
      b.edgeUs    = esp_timer_get_time();
      b.edgeSeq++;
    }
    const int64_t  edgeUs = b.edgeUs;
    const uint32_t seq    = b.edgeSeq;
    portEXIT_CRITICAL(&mux);

    // millis() is this same microsecond counter divided down, so the two stay
    // comparable, wrap included.
    const uint32_t edgeMs = (uint32_t)(edgeUs / 1000);
    const uint32_t now    = millis();

    // Any edge at all restarts the debounce window, even one that leaves the
    // level where it was: a press followed by a bounce back and forth between
    // two polls has settled at the last edge, not the first.
    if (seq != b.seenSeq || level != b.raw) {
      b.seenSeq    = seq;
      b.raw        = level;
      b.rawSinceMs = edgeMs;
    }

    // The comparison is signed because a timestamp taken just after `now` is
    // legitimate -- the sampling path above reads the clock a moment later.
    if (b.raw != b.stable &&
        (int32_t)(now - b.rawSinceMs) >= (int32_t)BUTTON_DEBOUNCE_MS) {
      b.stable = b.raw;
      if (b.stable) {
        // Straight from the interrupt, so the hold is measured from the moment
        // of contact even when this is the first poll after a long stall.
        b.pressedAt = b.rawSinceMs;
        b.holdFired = false;
      }
    }
  }
}

bool heldFor(Id id, uint32_t ms) {
  Button &b = buttons[id];
  if (!b.stable || b.holdFired ||
      (int32_t)(millis() - b.pressedAt) < (int32_t)ms) {
    return false;
  }
  b.holdFired = true;
  return true;
}

bool isDown(Id id) { return buttons[id].stable; }

bool anyDown() {
  for (uint8_t i = 0; i < COUNT; i++) {
    if (buttons[i].stable) {
      return true;
    }
  }
  return false;
}

void waitForRelease() {
  const uint32_t deadline = millis() + BUTTON_RELEASE_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    poll();
    if (!anyDown()) {
      return;
    }
    delay(BUTTON_POLL_MS);
  }
}

void armLightSleepWake() {
  for (uint8_t i = 0; i < COUNT; i++) {
    const gpio_num_t pin = (gpio_num_t)buttons[i].pin;
    // gpio_wakeup_enable() rewrites the pin's trigger to a level, and a level
    // stays asserted for as long as the button is down, so the edge handler
    // has to come down first or it would be re-entered without end. Nothing is
    // lost by that: the wake itself is the event, and poll() runs again the
    // moment we are back.
    gpio_intr_disable(pin);
    gpio_wakeup_enable(pin, BTN_LIGHT_SLEEP_WAKE_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
}

void disarmLightSleepWake() {
  for (uint8_t i = 0; i < COUNT; i++) {
    const gpio_num_t pin = (gpio_num_t)buttons[i].pin;
    gpio_wakeup_disable(pin);
    // Disabling the wake leaves the level trigger behind, so put the edge
    // trigger back before the handler is armed again.
    gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    gpio_intr_enable(pin);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}

} // namespace Buttons
