// Host test for the countdown digit placement in RefSegments.cpp. Pure
// arithmetic: no Arduino headers, no panel, so it runs anywhere.
#include "RefSegments.h"
#include <cstdio>

static int failures = 0;

// The two styles RefDisplay actually uses, repeated here so the test pins the
// numbers the panel is laid out around.
static const SegStyle BIG   = {70, 116, 15, 16};
static const SegStyle SMALL = {32, 52, 8, 8};
static const int16_t  SCREEN_W = 200;

static void expectEq(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-44s want %d got %d\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-44s %d\n", what, got);
  }
}

// Checks every value 0..199 for one style and prints a single summary line,
// rather than one line per value -- 400 "ok" lines (200 values x 2 styles)
// would drown the rest of the output. A failing value still gets its own
// line, so a regression is never silent.
static void expectAllFit(const char *styleName, const SegStyle &s) {
  int  localFailures = 0;
  char what[64];
  for (uint16_t v = 0; v <= 199; v++) {
    const CountLayout l     = layoutCount(v, s, SCREEN_W);
    const int16_t     left  = l.hundreds ? l.oneX : l.tensX;
    const int16_t     right = l.onesX + s.w;
    if (left < 0 || right > SCREEN_W) {
      snprintf(what, sizeof(what), "%s %u on panel", styleName, (unsigned)v);
      printf("FAIL %-44s spans %d..%d\n", what, left, right);
      localFailures++;
    }
  }
  snprintf(what, sizeof(what), "%s on panel, every value 0..199", styleName);
  if (localFailures == 0) {
    printf("ok   %-44s all 200 values fit 0..%d\n", what, SCREEN_W);
  }
  failures += localFailures;
}

int main() {
  // Two digits: unchanged from what the panel draws today.
  CountLayout l = layoutCount(40, BIG, SCREEN_W);
  expectEq("40 big: no hundreds bar", l.hundreds, 0);
  expectEq("40 big: tens digit", l.tens, 4);
  expectEq("40 big: ones digit", l.ones, 0);
  expectEq("40 big: width", l.width, 156);
  expectEq("40 big: tens x", l.tensX, 22);
  expectEq("40 big: ones x", l.onesX, 108);

  // Three digits: skinny 1 at the left, pair shifted right.
  l = layoutCount(120, BIG, SCREEN_W);
  expectEq("120 big: hundreds bar", l.hundreds, 1);
  expectEq("120 big: tens digit", l.tens, 2);
  expectEq("120 big: ones digit", l.ones, 0);
  expectEq("120 big: width", l.width, 187);
  expectEq("120 big: one x", l.oneX, 6);
  expectEq("120 big: tens x", l.tensX, 37);
  expectEq("120 big: ones x", l.onesX, 123);

  // Boundaries either side of the layout change.
  expectEq("100 big: hundreds bar", layoutCount(100, BIG, SCREEN_W).hundreds, 1);
  expectEq("99 big: no hundreds bar", layoutCount(99, BIG, SCREEN_W).hundreds, 0);

  // Above the ceiling, clamp rather than draw a fourth digit.
  l = layoutCount(200, BIG, SCREEN_W);
  expectEq("200 big: clamps to 199 tens", l.tens, 9);
  expectEq("200 big: clamps to 199 ones", l.ones, 9);
  expectEq("200 big: clamps to 199 hundreds", l.hundreds, 1);

  // Zero still draws two digits.
  l = layoutCount(0, BIG, SCREEN_W);
  expectEq("0 big: tens digit", l.tens, 0);
  expectEq("0 big: ones digit", l.ones, 0);
  expectEq("0 big: no hundreds bar", l.hundreds, 0);

  // The small style is what the ready screen stacks; 120 has to fit there too.
  l = layoutCount(120, SMALL, SCREEN_W);
  expectEq("120 small: width", l.width, 88);
  expectEq("120 small: one x", l.oneX, 56);

  // Nothing may run off either edge of the panel, at any value, in either
  // style RefDisplay actually draws with. SMALL matters here as much as BIG:
  // the Ready screen stacks both of the active preset's clocks in SMALL, and
  // Lacrosse and Base NCAA both reach 120, with Custom reaching 199.
  expectAllFit("BIG", BIG);
  expectAllFit("SMALL", SMALL);

  // Segment row arithmetic must stay in sync between drawDigit and drawOneBar.
  // These are the concrete numbers BIG and SMALL use.
  SegRows bigRows = layoutRows(BIG);
  expectEq("BIG: midY", bigRows.midY, 50);
  expectEq("BIG: upY", bigRows.upY, 15);
  expectEq("BIG: upH", bigRows.upH, 35);
  expectEq("BIG: lowY", bigRows.lowY, 65);
  expectEq("BIG: lowH", bigRows.lowH, 36);

  SegRows smallRows = layoutRows(SMALL);
  expectEq("SMALL: midY", smallRows.midY, 22);
  expectEq("SMALL: upY", smallRows.upY, 8);
  expectEq("SMALL: upH", smallRows.upH, 14);
  expectEq("SMALL: lowY", smallRows.lowY, 30);
  expectEq("SMALL: lowH", smallRows.lowH, 14);

  // The critical invariants: upper run ends where middle begins, lower run
  // starts one thickness below and ends one above the digit bottom.
  auto checkInvariant = [](const char *name, const SegStyle &s, const SegRows &r) {
    if (r.upY + r.upH != r.midY) {
      printf("FAIL %-44s upper run doesn't end at midY\n", name);
      failures++;
    } else if (r.lowY != r.midY + s.t) {
      printf("FAIL %-44s lower run doesn't start at midY+t\n", name);
      failures++;
    } else if (r.lowY + r.lowH != s.h - s.t) {
      printf("FAIL %-44s lower run doesn't end at h-t\n", name);
      failures++;
    } else {
      printf("ok   %-44s row invariants hold\n", name);
    }
  };
  checkInvariant("BIG row invariants", BIG, bigRows);
  checkInvariant("SMALL row invariants", SMALL, smallRows);

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
