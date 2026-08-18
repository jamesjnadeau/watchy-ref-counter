#include "Buzzer.h"

#include "board.h"

namespace Buzzer {

void begin() {
  pinMode(PIN_VIB_MOTOR, OUTPUT);
  digitalWrite(PIN_VIB_MOTOR, LOW);
}

void pulse(uint32_t onMs) {
  if (onMs == 0) {
    return;
  }
  digitalWrite(PIN_VIB_MOTOR, HIGH);
  delay(onMs);
  digitalWrite(PIN_VIB_MOTOR, LOW);
}

void pulse(uint8_t count, uint32_t onMs, uint32_t gapMs) {
  for (uint8_t i = 0; i < count; i++) {
    pulse(onMs);
    if (i + 1 < count) {
      delay(gapMs);
    }
  }
}

void off() { digitalWrite(PIN_VIB_MOTOR, LOW); }

} // namespace Buzzer
