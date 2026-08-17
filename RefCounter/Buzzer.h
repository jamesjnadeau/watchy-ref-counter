#ifndef REF_BUZZER_H
#define REF_BUZZER_H

#include <stdint.h>

// The vibration motor.
//
// Buzzes are blocking on purpose. The buzz is the signal an official actually
// reacts to, so it fires before the screen is redrawn and its length stays
// exact rather than being stretched by an e-paper refresh. Blocking does not
// make the clock drift: remaining time is always derived from the absolute
// start timestamp, never accumulated tick by tick.
namespace Buzzer {

void begin();

// One buzz of the given length. A length of 0 does nothing.
void pulse(uint32_t onMs);

// `count` buzzes of `onMs`, separated by `gapMs` of silence. The trailing gap
// after the last buzz is skipped.
void pulse(uint8_t count, uint32_t onMs, uint32_t gapMs);

// Force the motor off. Called before sleeping so a buzz can never be left on.
void off();

} // namespace Buzzer

#endif // REF_BUZZER_H
