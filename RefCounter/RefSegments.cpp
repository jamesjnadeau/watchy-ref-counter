#include "RefSegments.h"

SegRows layoutRows(const SegStyle &s) {
  SegRows r = {};
  r.midY = (s.h - s.t) / 2;
  r.upY  = s.t;
  r.upH  = r.midY - s.t;
  r.lowY = r.midY + s.t;
  r.lowH = s.h - s.t - r.lowY;
  return r;
}

CountLayout layoutCount(uint16_t value, const SegStyle &s, int16_t screenW) {
  if (value > SEG_MAX_VALUE) {
    value = SEG_MAX_VALUE;
  }

  CountLayout l = {};
  l.hundreds = value >= 100;
  l.tens     = (uint8_t)((value / 10) % 10);
  l.ones     = (uint8_t)(value % 10);

  const int16_t pairW = (int16_t)(s.w * 2 + s.gap);
  l.width = l.hundreds ? (int16_t)(s.t + s.gap + pairW) : pairW;

  const int16_t x = (int16_t)((screenW - l.width) / 2);
  l.oneX  = x;
  l.tensX = l.hundreds ? (int16_t)(x + s.t + s.gap) : x;
  l.onesX = (int16_t)(l.tensX + s.w + s.gap);
  return l;
}
