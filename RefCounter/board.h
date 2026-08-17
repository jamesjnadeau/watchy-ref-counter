#ifndef REF_BOARD_H
#define REF_BOARD_H

// ---------------------------------------------------------------------------
// Hardware differences between Watchy revisions, in one place.
//
// Pin numbers themselves come from the Watchy library's config.h, which picks
// them from the board define (ARDUINO_WATCHY_V10 / _V15 / _V20 for the ESP32
// boards, ARDUINO_ESP32S3_DEV for V3). This header only adds the two things
// config.h does not spell out: which logic level means "pressed", and which
// physical button plays which role.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <Watchy.h>  // pulls in config.h, Display.h and the DSEG7 font

#ifdef ARDUINO_ESP32S3_DEV
// V3 buttons pull the pin low when pressed.
#define BTN_PRESSED_LEVEL LOW
#define BTN_EXT1_WAKE_MODE ESP_EXT1_WAKEUP_ANY_LOW
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_LOW_LEVEL
#else
// V1 / V1.5 / V2 buttons pull the pin high when pressed.
#define BTN_PRESSED_LEVEL HIGH
#define BTN_EXT1_WAKE_MODE ESP_EXT1_WAKEUP_ANY_HIGH
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_HIGH_LEVEL
#endif

// --- Button roles ----------------------------------------------------------
// Watchy's four buttons, by physical position:
//
//     MENU (top left)  ---- +-------+ ---- UP   (top right)
//                           | 200x  |
//     BACK (bottom left) -- | 200   | ---- DOWN (bottom right)
//                           +-------+
//
// If the buttons on your unit are rotated relative to this, swap the three
// defines below rather than editing the sketch.
#define BTN_LONG_TIMER_PIN  UP_BTN_PIN    // top right    -> long clock (40s)
#define BTN_SHORT_TIMER_PIN DOWN_BTN_PIN  // bottom right -> short clock (25s)
#define BTN_SLEEP_PIN       MENU_BTN_PIN  // top left     -> low power mode

// --- Battery ADC -----------------------------------------------------------
// V3 measures through a 360k/100k divider; the ESP32 boards use a plain 2:1.
#ifdef ARDUINO_ESP32S3_DEV
#define BATT_DIVIDER ((360.0f + 100.0f) / 360.0f)
#else
#define BATT_DIVIDER 2.0f
#endif

// Empty / full battery, in volts, for the header gauge.
#define BATT_MIN_V 3.30f
#define BATT_MAX_V 4.20f

#endif // REF_BOARD_H
