#ifndef REF_BUTTONS_H
#define REF_BUTTONS_H

#include <stdint.h>

// Debounced button reader with one-shot hold detection.
//
// Only holds matter to this sketch: a short tap is never an action, so that a
// button brushed against a sleeve mid-game cannot reset the play clock.
//
// Edges are caught by an interrupt handler, which records which way the pin
// went and when. poll() debounces from those timestamps, so a press that lands
// while the sketch is busy elsewhere -- inside a 2.6s panel refresh, say -- is
// still measured from the real moment of contact rather than from whenever the
// sketch next got a chance to look. poll() also samples the pins directly, so
// a lost edge costs one poll interval rather than a button that has gone deaf.
namespace Buttons {

enum Id : uint8_t {
  LONG_TIMER = 0,  // top right
  SHORT_TIMER,     // bottom right
  SLEEP,           // top left
  RESET,           // bottom left
  COUNT
};

// Sets the pins up, seeds the debounce state and attaches the edge handlers.
void begin();

// Re-seed the debounce state from the pins as they are right now, suppressing
// a hold for any button already down. Used after code that has read the pins
// behind this module's back, so a button still settling is not mistaken for a
// fresh press.
void resync();

// Sample every button. Call once per loop iteration, and from anywhere that is
// about to block for longer than a hold threshold.
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

// Make the four buttons wake the chip from light sleep, and undo that on the
// way out. These own the pins' interrupt configuration, which is why they live
// here rather than in the sketch: arming for a light sleep wake rewrites the
// trigger, and the edge handlers have to be put back afterwards.
void armLightSleepWake();
void disarmLightSleepWake();

} // namespace Buttons

#endif // REF_BUTTONS_H
