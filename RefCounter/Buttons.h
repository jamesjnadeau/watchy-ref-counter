#ifndef REF_BUTTONS_H
#define REF_BUTTONS_H

#include <stdint.h>

// Debounced button reader with one-shot hold detection.
//
// Only holds matter to this sketch: a short tap is never an action, so that a
// button brushed against a sleeve mid-game cannot reset the play clock.
namespace Buttons {

enum Id : uint8_t {
  LONG_TIMER = 0,  // top right
  SHORT_TIMER,     // bottom right
  SLEEP,           // top left
  COUNT
};

void begin();

// Sample every button. Call once per loop iteration.
void poll();

// True exactly once per press, the moment the button has been held for `ms`.
// Holding longer does not fire again; the button must be released first.
bool heldFor(Id id, uint32_t ms);

// True while the given button is down.
bool isDown(Id id);

// True while any button is down.
bool anyDown();

// Block until every button is released, or until BUTTON_RELEASE_TIMEOUT_MS
// elapses. Used before sleeping so a still-held button cannot immediately
// wake the watch back up.
void waitForRelease();

} // namespace Buttons

#endif // REF_BUTTONS_H
