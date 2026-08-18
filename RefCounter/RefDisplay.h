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
  uint8_t  hour;         // wall clock from the RTC, shown top left
  uint8_t  minute;
  bool     clockValid;   // false renders --:-- instead
};

// Theme colours, derived from DARK_MODE. Declared here so the settings menu
// paints in the same scheme as the play clock.
extern const uint16_t THEME_FG;
extern const uint16_t THEME_BG;

namespace RefDisplay {

void begin();

// Redraw the whole screen.
//
// `full` picks the refresh waveform, not the amount drawn. A full refresh
// takes about 2.6s and clears ghosting; a partial refresh takes roughly 400ms
// and leaves faint residue behind. Per-second updates must be partial.
void render(const View &v, bool full);

// Draw the whole screen into the framebuffer without refreshing the panel.
// render() is this plus a refresh; it is exposed separately so a caller that
// wants to choose its own refresh scope can.
void paint(const View &v);

// Repaint only the countdown digits, and refresh only that window of the
// panel. Roughly a third of the pixels of a whole-screen partial refresh, so
// it is quicker and leaves far less ghosting behind.
//
// Requires that render() has painted the full screen at least once: the header
// and footer are left standing in the framebuffer and are not redrawn.
void renderDigits(const View &v);

// Repaint only the header strip -- the clock and the battery gauge -- and
// refresh just that window. Used once a minute so the time stays live without
// redrawing anything else.
void renderHeader(const View &v);

// The low power screen. Always a full refresh, since it is the last thing
// drawn before the panel is powered down and it may sit there for hours.
void renderSleeping();

// Power the panel down completely. Call before deep sleep.
void hibernate();

// A zero-padded two-digit value in the same seven-segment face as the
// countdown, at a size suited to the menu's set-time screen. `lit` false draws
// it in the background colour, which is how that screen blinks a field.
void drawDigitPair(int16_t x, int16_t y, uint8_t value, bool lit);
extern const int16_t DIGIT_PAIR_W;
extern const int16_t DIGIT_PAIR_H;

} // namespace RefDisplay

#endif // REF_DISPLAY_H
