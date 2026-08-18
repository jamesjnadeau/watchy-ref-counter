#include "RefDisplay.h"

#include "RefPanel.h"
#include "board.h"
#include "settings.h"

extern const uint16_t THEME_FG = DARK_MODE ? GxEPD_WHITE : GxEPD_BLACK;
extern const uint16_t THEME_BG = DARK_MODE ? GxEPD_BLACK : GxEPD_WHITE;

namespace RefDisplay {
namespace {

auto &display = RefPanel::display;

const uint16_t &FG = THEME_FG;
const uint16_t &BG = THEME_BG;

// --- Layout, in pixels on the 200x200 panel --------------------------------
const int16_t SCREEN_W = DISPLAY_WIDTH;

const int16_t HEADER_BASELINE = 26;
const int16_t HEADER_RULE_Y   = 36;

const int16_t BIG_Y  = 44;  // top of the large countdown (RUNNING / EXPIRED)
const int16_t ROW1_Y = 44;  // top of the upper value on the idle screen
const int16_t ROW2_Y = 106; // top of the lower value on the idle screen

const int16_t FOOTER_RULE_Y   = 166;
const int16_t FOOTER_BASELINE = 188;

const int16_t BATT_X = 166;
const int16_t BATT_Y = 14;
const int16_t BATT_W = 26;
const int16_t BATT_H = 12;

// The window refreshed on each tick. It has to cover the large digits, which
// span x 22..178 and y 44..160. GxEPD2 rounds x and w out to a multiple of 8
// anyway, so they are pre-aligned here to keep the drawn and refreshed areas
// honest with each other.
// The header strip, refreshed on its own when the minute rolls over. Covers
// the clock, the battery gauge and the rule underneath them.
const int16_t HEADER_WIN_X = 0;
const int16_t HEADER_WIN_Y = 0;
const int16_t HEADER_WIN_W = 200;
const int16_t HEADER_WIN_H = 40;

const int16_t DIGITS_WIN_X = 16;
const int16_t DIGITS_WIN_Y = 40;
const int16_t DIGITS_WIN_W = 176;
const int16_t DIGITS_WIN_H = 124;

// --- Seven segment digits --------------------------------------------------
// Drawn from rectangles rather than a font, so the exact bounding box of the
// countdown is known at compile time. That is what makes the partial window
// above safe to hard-code, and it drops the 14KB DSEG7 font from the build.
//
//     --a--
//    |     |
//    f     b
//    |     |
//     --g--
//    |     |
//    e     c
//    |     |
//     --d--
struct SegStyle {
  int16_t w;   // width of one digit
  int16_t h;   // height of one digit
  int16_t t;   // segment thickness
  int16_t gap; // space between the two digits
};

const SegStyle STYLE_BIG   = {70, 116, 15, 16}; // 156 x 116 for two digits
const SegStyle STYLE_SMALL = {32, 52, 8, 8};    // 72 x 52 for two digits

// Which segments are lit for each digit. Bit order a,b,c,d,e,f,g.
const uint8_t SEG_A = 0x01, SEG_B = 0x02, SEG_C = 0x04, SEG_D = 0x08,
              SEG_E = 0x10, SEG_F = 0x20, SEG_G = 0x40;

const uint8_t DIGIT_SEGMENTS[10] = {
    /* 0 */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    /* 1 */ SEG_B | SEG_C,
    /* 2 */ SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,
    /* 3 */ SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,
    /* 4 */ SEG_F | SEG_G | SEG_B | SEG_C,
    /* 5 */ SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
    /* 6 */ SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,
    /* 7 */ SEG_A | SEG_B | SEG_C,
    /* 8 */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    /* 9 */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
};

void drawDigit(int16_t x, int16_t y, uint8_t value, const SegStyle &s,
               uint16_t colour) {
  const uint8_t on = DIGIT_SEGMENTS[value % 10];

  const int16_t t     = s.t;
  const int16_t midY  = (s.h - t) / 2; // top edge of the middle bar
  const int16_t upH   = midY - t;      // height of the upper verticals
  const int16_t lowY  = midY + t;      // top of the lower verticals
  const int16_t lowH  = s.h - t - lowY;
  const int16_t barW  = s.w - 2 * t;   // length of the horizontal bars
  const int16_t right = x + s.w - t;

  if (on & SEG_A) display.fillRect(x + t, y, barW, t, colour);
  if (on & SEG_B) display.fillRect(right, y + t, t, upH, colour);
  if (on & SEG_C) display.fillRect(right, y + lowY, t, lowH, colour);
  if (on & SEG_D) display.fillRect(x + t, y + s.h - t, barW, t, colour);
  if (on & SEG_E) display.fillRect(x, y + lowY, t, lowH, colour);
  if (on & SEG_F) display.fillRect(x, y + t, t, upH, colour);
  if (on & SEG_G) display.fillRect(x + t, y + midY, barW, t, colour);
}

void drawPair(int16_t x, int16_t y, uint16_t value, const SegStyle &s,
              uint16_t colour) {
  if (value > 99) {
    value = 99;
  }
  drawDigit(x, y, (uint8_t)(value / 10), s, colour);
  drawDigit(x + s.w + s.gap, y, (uint8_t)(value % 10), s, colour);
}

// A zero-padded two digit value, centred horizontally.
void drawValue(uint16_t value, int16_t y, const SegStyle &s) {
  drawPair((SCREEN_W - (s.w * 2 + s.gap)) / 2, y, value, s, FG);
}

float batteryVolts() {
  return (analogReadMilliVolts(PIN_BATT_ADC) / 1000.0f) * BATT_DIVIDER;
}

// Draw `text` horizontally centred on `centreX`, at the given baseline.
void printCentred(const char *text, int16_t baselineY, int16_t centreX = SCREEN_W / 2) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);
  // x1 is where the glyphs actually start when the cursor sits at 0, which for
  // a custom font is rarely 0. Subtract it so the ink lands centred, not the
  // cursor.
  display.setCursor(centreX - (int16_t)(w / 2) - x1, baselineY);
  display.print(text);
}

void drawBattery() {
  const float v = batteryVolts();
  float pct = (v - BATT_MIN_V) / (BATT_MAX_V - BATT_MIN_V);
  pct = pct < 0.0f ? 0.0f : (pct > 1.0f ? 1.0f : pct);

  display.drawRect(BATT_X, BATT_Y, BATT_W, BATT_H, FG);
  display.fillRect(BATT_X + BATT_W, BATT_Y + 3, 3, BATT_H - 6, FG);

  const int16_t fill = (int16_t)((BATT_W - 4) * pct);
  if (fill > 0) {
    display.fillRect(BATT_X + 2, BATT_Y + 2, fill, BATT_H - 4, FG);
  }
}

void drawHeader(const View &v) {
  char clock[8];
  if (!v.clockValid) {
    snprintf(clock, sizeof(clock), "--:--");
  } else if (CLOCK_24_HOUR) {
    snprintf(clock, sizeof(clock), "%02u:%02u", v.hour, v.minute);
  } else {
    uint8_t h = v.hour % 12;
    if (h == 0) {
      h = 12;
    }
    snprintf(clock, sizeof(clock), "%u:%02u", h, v.minute);
  }

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(4, HEADER_BASELINE);
  display.print(clock);

  drawBattery();
  display.drawFastHLine(0, HEADER_RULE_Y, SCREEN_W, FG);
}

// A solid triangle at the right edge, pointing at the button that starts the
// value on this row.
void drawRowMarker(int16_t rowY, const SegStyle &s) {
  const int16_t cy = rowY + s.h / 2;
  display.fillTriangle(SCREEN_W - 4, cy, SCREEN_W - 16, cy - 8, SCREEN_W - 16,
                       cy + 8, FG);
}

void drawBody(const View &v) {
  if (v.state == STATE_IDLE) {
    // Stack both clocks so they line up with the two right-hand buttons.
    drawValue(TIMER_LONG_SECONDS, ROW1_Y, STYLE_SMALL);
    drawRowMarker(ROW1_Y, STYLE_SMALL);
    drawValue(TIMER_SHORT_SECONDS, ROW2_Y, STYLE_SMALL);
    drawRowMarker(ROW2_Y, STYLE_SMALL);
    return;
  }
  drawValue(v.secondsLeft, BIG_Y, STYLE_BIG);
}

void drawFooter(const View &v) {
  const char *hint;
  switch (v.state) {
  case STATE_RUNNING:
    hint = "BTM LEFT = CLEAR";
    break;
  case STATE_EXPIRED:
    hint = "TIME EXPIRED";
    break;
  default:
    hint = "HOLD TO START";
    break;
  }

  display.drawFastHLine(0, FOOTER_RULE_Y, SCREEN_W, FG);
  display.setFont(&FreeMonoBold9pt7b);
  printCentred(hint, FOOTER_BASELINE);
}

const SegStyle STYLE_MED = {40, 66, 10, 10};

} // namespace

extern const int16_t DIGIT_PAIR_W = STYLE_MED.w * 2 + STYLE_MED.gap;
extern const int16_t DIGIT_PAIR_H = STYLE_MED.h;

void drawDigitPair(int16_t x, int16_t y, uint8_t value, bool lit) {
  drawPair(x, y, value, STYLE_MED, lit ? THEME_FG : THEME_BG);
}

void begin() {
  RefPanel::begin();
  display.setTextColor(FG);
  display.setTextWrap(false);
}

void paint(const View &v) {
  display.setFullWindow();
  display.fillScreen(BG);
  display.setTextColor(FG);

  drawHeader(v);
  drawBody(v);
  drawFooter(v);
}

void render(const View &v, bool full) {
  paint(v);
  // GxEPD2 takes "partial refresh?" here, so a full refresh is display(false).
  display.display(!full);
}

void renderHeader(const View &v) {
  display.setTextColor(FG);
  display.fillRect(HEADER_WIN_X, HEADER_WIN_Y, HEADER_WIN_W, HEADER_WIN_H, BG);
  drawHeader(v);
  display.displayWindow(HEADER_WIN_X, HEADER_WIN_Y, HEADER_WIN_W, HEADER_WIN_H);
}

void renderDigits(const View &v) {
  display.setTextColor(FG);
  display.fillRect(DIGITS_WIN_X, DIGITS_WIN_Y, DIGITS_WIN_W, DIGITS_WIN_H, BG);
  drawValue(v.secondsLeft, BIG_Y, STYLE_BIG);
  display.displayWindow(DIGITS_WIN_X, DIGITS_WIN_Y, DIGITS_WIN_W, DIGITS_WIN_H);
}

void renderSleeping() {
  display.setFullWindow();
  display.fillScreen(BG);
  display.setTextColor(FG);

  display.setFont(&FreeMonoBold18pt7b);
  printCentred("SLEEPING", 110);

  display.setFont(&FreeMonoBold9pt7b);
  printCentred("TOP LEFT TO WAKE", 150);

  display.display(false);
}

void hibernate() { display.hibernate(); }

} // namespace RefDisplay
