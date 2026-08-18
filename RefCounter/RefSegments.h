#ifndef REF_SEGMENTS_H
#define REF_SEGMENTS_H

#include <stdint.h>

// Where the countdown's seven-segment glyphs land on the panel.
//
// This is pure arithmetic with no Arduino or GxEPD2 dependency, so the layout
// can be checked on a host. RefDisplay owns the actual drawing; this file only
// decides where each glyph goes.
//
// Values of 100 and up get a skinny leading "1" -- just the two vertical bars,
// one segment thickness wide -- to the left of the two full-size digits, which
// shift right to make room. 199 is the ceiling; there is no room for a fourth
// glyph and no sport needs one.

// Geometry of one seven-segment digit and the space that follows it.
struct SegStyle {
  int16_t w;   // width of one digit
  int16_t h;   // height of one digit
  int16_t t;   // segment thickness
  int16_t gap; // space between adjacent glyphs
};

struct CountLayout {
  bool    hundreds; // draw the skinny leading 1
  int16_t oneX;     // left edge of that bar; ignore when !hundreds
  int16_t tensX;    // left edge of the tens digit
  int16_t onesX;    // left edge of the ones digit
  int16_t width;    // total ink width, so a caller can size a refresh window
  uint8_t tens;
  uint8_t ones;
};

// Largest value the layout can draw. Anything above is clamped to it.
static const uint16_t SEG_MAX_VALUE = 199;

// Lay `value` out horizontally centred on a `screenW` wide panel.
CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW);

#endif // REF_SEGMENTS_H
