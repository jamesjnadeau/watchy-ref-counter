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

// The vertical extents of a digit's segments, all derived from the style.
// Shared so the standalone hundreds bar and the full digits cannot drift out
// of alignment: both ask for the same rows rather than each recomputing them.
struct SegRows {
  int16_t midY; // top edge of the middle bar (segment g)
  int16_t upY;  // top edge of the upper verticals (segments b, f)
  int16_t upH;  // their height
  int16_t lowY; // top edge of the lower verticals (segments c, e)
  int16_t lowH; // their height
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

// Compute the vertical extents of seven-segment rows for a given style.
SegRows layoutRows(const SegStyle &s);

// Lay `value` out horizontally centred on a `screenW` wide panel.
CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW);

#endif // REF_SEGMENTS_H
