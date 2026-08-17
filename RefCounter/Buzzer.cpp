#include "Buzzer.h"

#include "board.h"

namespace Buzzer {

void begin() {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  digitalWrite(VIB_MOTOR_PIN, LOW);
}

void pulse(uint32_t onMs) {
  if (onMs == 0) {
    return;
  }
  digitalWrite(VIB_MOTOR_PIN, HIGH);
  delay(onMs);
  digitalWrite(VIB_MOTOR_PIN, LOW);
}

void pulse(uint8_t count, uint32_t onMs, uint32_t gapMs) {
  for (uint8_t i = 0; i < count; i++) {
    pulse(onMs);
    if (i + 1 < count) {
      delay(gapMs);
    }
  }
}

void off() { digitalWrite(VIB_MOTOR_PIN, LOW); }

} // namespace Buzzer
