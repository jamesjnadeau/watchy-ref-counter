#ifndef REF_BOARD_H
#define REF_BOARD_H

// ---------------------------------------------------------------------------
// Watchy hardware map.
//
// This repo is self-contained: it does not link against the Watchy library.
// The pin numbers, button polarity and battery divider ratios below are facts
// about the hardware, and were read off the schematics and the pin map in the
// reference firmware, sqfmi/Watchy (MIT licence), by way of credit. No code
// from that project is used here.
//
// The revision is selected by the build: ARDUINO_WATCHY_V10 / _V15 / _V20 for
// the ESP32 boards, ARDUINO_ESP32S3_DEV for V3. See platformio.ini.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// PlatformIO sets the revision from platformio.ini. The Arduino IDE has no
// build-flag field, so IDE users uncomment the line matching their hardware
// here -- it has to live in this header, which every source file includes,
// rather than in the .ino, which only sets it for itself.
//
// #define ARDUINO_WATCHY_V10
// #define ARDUINO_WATCHY_V15
// #define ARDUINO_WATCHY_V20
//
// V3 needs nothing: selecting an ESP32-S3 board defines ARDUINO_ESP32S3_DEV.

#if !defined(ARDUINO_ESP32S3_DEV) && !defined(ARDUINO_WATCHY_V10) &&           \
    !defined(ARDUINO_WATCHY_V15) && !defined(ARDUINO_WATCHY_V20)
#warning "No Watchy revision defined; assuming V2.0"
#define ARDUINO_WATCHY_V20
#endif

#ifdef ARDUINO_ESP32S3_DEV
// --- V3 (ESP32-S3) ---------------------------------------------------------
#define PIN_I2C_SDA 12
#define PIN_I2C_SCL 11

#define PIN_SPI_SCK  47
#define PIN_SPI_MISO 46
#define PIN_SPI_MOSI 48
#define PIN_SPI_SS   33

#define PIN_BTN_MENU 7
#define PIN_BTN_BACK 6
#define PIN_BTN_UP   0
#define PIN_BTN_DOWN 8

#define PIN_DISPLAY_CS   33
#define PIN_DISPLAY_DC   34
#define PIN_DISPLAY_RST  35
#define PIN_DISPLAY_BUSY 36

#define PIN_VIB_MOTOR 17
#define PIN_BATT_ADC  9

// Buttons pull their pin low when pressed.
#define BTN_PRESSED_LEVEL          LOW
#define BTN_EXT1_WAKE_MODE         ESP_EXT1_WAKEUP_ANY_LOW
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_LOW_LEVEL

// Battery is measured through a 360k / 100k divider.
#define BATT_DIVIDER ((360.0f + 100.0f) / 360.0f)

#else
// --- V1.0 / V1.5 / V2.0 (ESP32) --------------------------------------------
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 23

#define PIN_BTN_MENU 26
#define PIN_BTN_BACK 25
#define PIN_BTN_DOWN 4

#define PIN_DISPLAY_CS   5
#define PIN_DISPLAY_RST  9
#define PIN_DISPLAY_DC   10
#define PIN_DISPLAY_BUSY 19

#define PIN_VIB_MOTOR 13

// The up button and the battery tap moved between revisions.
#if defined(ARDUINO_WATCHY_V10)
#define PIN_BTN_UP   32
#define PIN_BATT_ADC 33
#elif defined(ARDUINO_WATCHY_V15)
#define PIN_BTN_UP   32
#define PIN_BATT_ADC 35
#else // ARDUINO_WATCHY_V20
#define PIN_BTN_UP   35
#define PIN_BATT_ADC 34
#endif

// Buttons pull their pin high when pressed.
#define BTN_PRESSED_LEVEL          HIGH
#define BTN_EXT1_WAKE_MODE         ESP_EXT1_WAKEUP_ANY_HIGH
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_HIGH_LEVEL

// Battery is measured through a plain 2:1 divider.
#define BATT_DIVIDER 2.0f

#endif

// --- Panel -----------------------------------------------------------------
#define DISPLAY_WIDTH  200
#define DISPLAY_HEIGHT 200

// --- Battery gauge ends, in volts ------------------------------------------
#define BATT_MIN_V 3.30f
#define BATT_MAX_V 4.20f

// --- Button roles ----------------------------------------------------------
// Watchy's four buttons, by physical position. Note that MENU is the BOTTOM
// left button and BACK is the TOP left one, which is the opposite of what the
// names suggest:
//
//     BACK (top left)  ----- +-------+ ---- UP   (top right)
//                            | 200x  |
//     MENU (bottom left) --- | 200   | ---- DOWN (bottom right)
//                            +-------+
//
// If the buttons on your unit sit differently, swap the four defines below
// rather than editing the sketch.
#define BTN_LONG_TIMER_PIN  PIN_BTN_UP    // top right    -> long clock (40s)
#define BTN_SHORT_TIMER_PIN PIN_BTN_DOWN  // bottom right -> short clock (25s)
#define BTN_SLEEP_PIN       PIN_BTN_BACK  // top left     -> low power mode
#define BTN_RESET_PIN       PIN_BTN_MENU  // bottom left  -> clear / menu

#endif // REF_BOARD_H
