#ifndef REF_DISPLAY_H
#define REF_DISPLAY_H

#include <stdint.h>

// What the watch is currently doing.
enum AppState : uint8_t {
  STATE_IDLE,     // no clock running, waiting for a button hold
  STATE_RUNNING,  // counting down
  STATE_EXPIRED,  // reached zero, showing 00 until the next clock is started
  STATE_SLEEPING, // low power mode
};

// Everything the screen needs to draw itself.
struct View {
  AppState state;
  uint16_t secondsLeft;  // RUNNING / EXPIRED only
  uint16_t durationSec;  // which clock is (or was) running
};

namespace RefDisplay {

void begin();

// Redraw the whole screen.
//
// `full` picks the refresh waveform, not the amount drawn. A full refresh
// takes about 2.6s and clears ghosting; a partial refresh takes roughly 400ms
// and leaves faint residue behind. Per-second updates must be partial.
void render(const View &v, bool full);

// Repaint only the countdown digits, and refresh only that window of the
// panel. Roughly a third of the pixels of a whole-screen partial refresh, so
// it is quicker and leaves far less ghosting behind.
//
// Requires that render() has painted the full screen at least once: the header
// and footer are left standing in the framebuffer and are not redrawn.
void renderDigits(const View &v);

// The low power screen. Always a full refresh, since it is the last thing
// drawn before the panel is powered down and it may sit there for hours.
void renderSleeping();

// Power the panel down completely. Call before deep sleep.
void hibernate();

} // namespace RefDisplay

#endif // REF_DISPLAY_H
