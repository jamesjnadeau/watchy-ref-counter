#ifndef REF_BUZZER_H
#define REF_BUZZER_H

#include <stdint.h>

// The vibration motor.
//
// Buzzes run in the background. pulse() starts the motor and returns straight
// away; an esp_timer callback stops it again. That callback runs in a task
// above the sketch's own, so a pattern keeps its shape even while the loop is
// stuck inside a panel refresh -- and, more to the point, the buttons stay live
// for the whole buzz instead of being deaf for the second the motor is on.
//
// Buzz length stays exact either way, and so does the clock: remaining time is
// always derived from the absolute start timestamp, never accumulated.
namespace Buzzer {

void begin();

// One buzz of the given length. A length of 0 does nothing.
void pulse(uint32_t onMs);

// `count` buzzes of `onMs`, separated by `gapMs` of silence. The trailing gap
// after the last buzz is skipped. A new call replaces whatever was running.
void pulse(uint8_t count, uint32_t onMs, uint32_t gapMs);

// True while a buzz, or a gap inside a pattern, is still outstanding.
bool busy();

// Force the motor off and drop anything still queued. Called before sleeping
// so a buzz can never be left running.
void off();

} // namespace Buzzer

#endif // REF_BUZZER_H
