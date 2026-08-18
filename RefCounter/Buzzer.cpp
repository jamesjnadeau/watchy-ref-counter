#include "Buzzer.h"

#include <esp_timer.h>

#include "board.h"

namespace Buzzer {
namespace {

esp_timer_handle_t timer = nullptr;

// The state below is touched from both the sketch and the timer task, so every
// read-modify-write on it sits inside this lock.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t  pulsesLeft = 0; // buzzes not yet started, current one excluded
uint32_t onMs       = 0;
uint32_t gapMs      = 0;
bool     motorOn    = false;

void motor(bool on) {
  motorOn = on;
  digitalWrite(PIN_VIB_MOTOR, on ? HIGH : LOW);
}

// Start the next buzz. The caller holds `mux`.
void startPulse() {
  pulsesLeft--;
  motor(true);
  esp_timer_start_once(timer, (uint64_t)onMs * 1000ULL);
}

// Runs in the esp_timer task, which sits at a priority well above the sketch's
// -- that is what lets a pattern keep running through a blocking refresh.
void onTimer(void *) {
  portENTER_CRITICAL(&mux);
  if (motorOn) {
    motor(false);
    if (pulsesLeft > 0) {
      // A zero gap is legal; the timer just fires again immediately.
      esp_timer_start_once(timer, (uint64_t)gapMs * 1000ULL);
    }
  } else if (pulsesLeft > 0) {
    startPulse();
  }
  portEXIT_CRITICAL(&mux);
}

} // namespace

void begin() {
  pinMode(PIN_VIB_MOTOR, OUTPUT);
  digitalWrite(PIN_VIB_MOTOR, LOW);

  if (timer != nullptr) {
    return;
  }
  const esp_timer_create_args_t args = {
      .callback              = &onTimer,
      .arg                   = nullptr,
      .dispatch_method       = ESP_TIMER_TASK,
      .name                  = "buzz",
      .skip_unhandled_events = false,
  };
  esp_timer_create(&args, &timer);
}

void pulse(uint32_t ms) { pulse(1, ms, 0); }

void pulse(uint8_t count, uint32_t ms, uint32_t gap) {
  if (timer == nullptr || count == 0 || ms == 0) {
    return;
  }
  esp_timer_stop(timer); // no-op unless a pattern is already running

  portENTER_CRITICAL(&mux);
  pulsesLeft = count;
  onMs       = ms;
  gapMs      = gap;
  startPulse();
  portEXIT_CRITICAL(&mux);
}

bool busy() {
  portENTER_CRITICAL(&mux);
  const bool running = motorOn || pulsesLeft > 0;
  portEXIT_CRITICAL(&mux);
  return running;
}

void off() {
  if (timer != nullptr) {
    esp_timer_stop(timer);
  }
  portENTER_CRITICAL(&mux);
  pulsesLeft = 0;
  motor(false);
  portEXIT_CRITICAL(&mux);
}

} // namespace Buzzer
