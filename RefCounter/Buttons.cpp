#include "Buttons.h"

#include "board.h"
#include "settings.h"

namespace Buttons {
namespace {

struct Button {
  uint8_t  pin;
  bool     raw;         // last sampled level, undebounced
  bool     stable;      // debounced level: true == pressed
  uint32_t lastEdgeAt;  // when `raw` last changed
  uint32_t pressedAt;   // when `stable` last went true
  bool     holdFired;   // hold action already delivered for this press
};

Button buttons[COUNT] = {
    {BTN_LONG_TIMER_PIN, false, false, 0, 0, false},
    {BTN_SHORT_TIMER_PIN, false, false, 0, 0, false},
    {BTN_SLEEP_PIN, false, false, 0, 0, false},
    {BTN_RESET_PIN, false, false, 0, 0, false},
};

bool readPin(uint8_t pin) { return digitalRead(pin) == BTN_PRESSED_LEVEL; }

} // namespace

void begin() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < COUNT; i++) {
    Button &b = buttons[i];
    pinMode(b.pin, INPUT);
    // Seed with the current level so a button already down at boot (the one
    // that just woke us, for instance) is not mistaken for a fresh press.
    b.raw = b.stable = readPin(b.pin);
    b.lastEdgeAt = now;
    b.pressedAt = now;
    b.holdFired = b.stable;
  }
}

void poll() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < COUNT; i++) {
    Button &b = buttons[i];
    const bool level = readPin(b.pin);

    if (level != b.raw) {
      b.raw = level;
      b.lastEdgeAt = now;
      continue;
    }
    if (level == b.stable || (now - b.lastEdgeAt) < BUTTON_DEBOUNCE_MS) {
      continue;
    }

    b.stable = level;
    if (b.stable) {
      // Back-date past the debounce window so the hold threshold is measured
      // from the real moment of contact.
      b.pressedAt = b.lastEdgeAt;
      b.holdFired = false;
    }
  }
}

bool heldFor(Id id, uint32_t ms) {
  Button &b = buttons[id];
  if (!b.stable || b.holdFired || (millis() - b.pressedAt) < ms) {
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

} // namespace Buttons
