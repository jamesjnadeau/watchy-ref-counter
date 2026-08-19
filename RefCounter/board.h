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
// the ESP32 boards, ARDUINO_ESP32S3_DEV for V3, ARDUINO_ESP32C6_DEV for this
// repo's own ESP32-C6-MINI-1 board. See platformio.ini.
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
// V3 and the C6 board need nothing: selecting an ESP32-S3 or ESP32-C6 board
// defines ARDUINO_ESP32S3_DEV / ARDUINO_ESP32C6_DEV.

#if !defined(ARDUINO_ESP32S3_DEV) && !defined(ARDUINO_ESP32C6_DEV) &&          \
    !defined(ARDUINO_WATCHY_V10) && !defined(ARDUINO_WATCHY_V15) &&            \
    !defined(ARDUINO_WATCHY_V20)
#warning "No Watchy revision defined; assuming V2.0"
#define ARDUINO_WATCHY_V20
#endif

#ifdef ARDUINO_ESP32C6_DEV
// --- watchy-ref-counter's own board (ESP32-C6-MINI-1 / RV-3028-C7) ---------
// Every number here comes from board-files/elec/src/watchy.ato. Change them
// there and here together; board-files/checks/netlist_check.py asserts the
// netlist half, and nothing asserts this half.
//
// The C6 brings out 22 GPIO and only IO0-IO7 can wake it from deep sleep, so
// the four buttons and the RTC alarm take five of those eight and everything
// else lives higher up. See mcu.ato for the datasheet citations.
#define PIN_I2C_SDA 7
#define PIN_I2C_SCL 6

// The six display signals are assigned so that they leave the module in the
// same left-to-right order the panel's FPC presents them, which is what makes
// the fan-out planar. MISO is a spare, unconnected pin: the panel is
// write-only and nothing drives the line.
#define PIN_SPI_SCK  15
#define PIN_SPI_MISO 23
#define PIN_SPI_MOSI 14
#define PIN_SPI_SS   8

// IO0-IO3. All four are LP GPIOs, so one ESP_EXT1_WAKEUP_ANY_LOW mask covers
// them and the RTC alarm on IO4.
#define PIN_BTN_MENU 0
#define PIN_BTN_BACK 1
#define PIN_BTN_UP   2
#define PIN_BTN_DOWN 3

#define PIN_DISPLAY_CS   8
#define PIN_DISPLAY_DC   18
#define PIN_DISPLAY_RST  19
#define PIN_DISPLAY_BUSY 20

#define PIN_VIB_MOTOR 21
#define PIN_BATT_ADC  5

// Buttons pull their pin low when pressed.
#define BTN_PRESSED_LEVEL          LOW
#define BTN_EXT1_WAKE_MODE         ESP_EXT1_WAKEUP_ANY_LOW
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_LOW_LEVEL

// 1M/1M divider, as on the S3 board -- 2:1, and 2.1uA of standing drain.
#define BATT_DIVIDER 2.0f

#elif defined(ARDUINO_ESP32S3_DEV)
// --- V3 (ESP32-S3) ---------------------------------------------------------
#define PIN_I2C_SDA 12
#define PIN_I2C_SCL 11

// SCK/MOSI/CS follow elec/src/watchy.ato, where the six display signals are
// assigned to module pins 27..32 in the same left-to-right order the panel's
// FPC presents them, so the fan-out does not cross itself. Change them there
// and here together.
#define PIN_SPI_SCK  33
#define PIN_SPI_MISO 46
#define PIN_SPI_MOSI 47
#define PIN_SPI_SS   34

// IO4..IO7, per elec/src/watchy.ato and the spec's pin map. These were 7/6/0/8
// -- carried over from a different revision and wrong for this board on every
// line. IO0 is the boot strapping pin and is TP_BOOT here, so "UP" would have
// pulled it low; MENU read BTN_DOWN, BACK read BTN_UP, and DOWN read RTC_INT.
#define PIN_BTN_MENU 4
#define PIN_BTN_BACK 5
#define PIN_BTN_UP   6
#define PIN_BTN_DOWN 7

#define PIN_DISPLAY_CS   34
#define PIN_DISPLAY_DC   48
#define PIN_DISPLAY_RST  35
#define PIN_DISPLAY_BUSY 36

#define PIN_VIB_MOTOR 17
#define PIN_BATT_ADC  9

// Buttons pull their pin low when pressed.
#define BTN_PRESSED_LEVEL          LOW
#define BTN_EXT1_WAKE_MODE         ESP_EXT1_WAKEUP_ANY_LOW
#define BTN_LIGHT_SLEEP_WAKE_LEVEL GPIO_INTR_LOW_LEVEL

// Battery is measured through a 360k / 100k divider.
// This board's divider is 1M/1M -- a 2:1 ratio, chosen over V2's 100k/100k to
// cut the standing drain from 21uA to 2.1uA. The 360k/100k expression here was
// another revision's and gave 1.28 instead of 2.0, reading the battery ~36%
// low. See elec/src/watchy.ato, "V2's two 100k draw 4.2V/200k".
#define BATT_DIVIDER 2.0f

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
