// The pin map is a fact about the hardware, not a preference, so it gets a
// test that fails loudly if a define moves. These are the V2.0 numbers; the
// C6 board's are in board_c6_test.cpp.
//
// V1.0, V1.5 and V3 are no longer supported and board.h #errors on their
// flags. That half is checked by tests/run.sh, because a compile that has to
// fail cannot live in a file that has to compile.

#define ARDUINO_WATCHY_V20 // as platformio.ini's watchy_v2 env does

#include <stdio.h>

#include "board.h"

static_assert(PIN_BTN_MENU == 26, "V2.0 menu button is GPIO 26");
static_assert(PIN_BTN_BACK == 25, "V2.0 back button is GPIO 25");
static_assert(PIN_BTN_DOWN == 4, "V2.0 down button is GPIO 4");
static_assert(PIN_BTN_UP == 35, "V2.0 up button is GPIO 35, not V1's 32");
static_assert(PIN_BATT_ADC == 34, "V2.0 battery tap is GPIO 34, not V1.0's 33 "
                                  "or V1.5's 35");
static_assert(PIN_I2C_SDA == 21 && PIN_I2C_SCL == 22, "V2.0 I2C pins");
static_assert(PIN_SPI_SCK == 18 && PIN_SPI_MOSI == 23, "V2.0 SPI pins");
static_assert(PIN_DISPLAY_CS == 5 && PIN_DISPLAY_RST == 9 &&
                  PIN_DISPLAY_DC == 10 && PIN_DISPLAY_BUSY == 19,
              "V2.0 panel control pins");
static_assert(PIN_VIB_MOTOR == 13, "V2.0 vibration motor is GPIO 13");
static_assert(BTN_PRESSED_LEVEL == HIGH,
              "the ESP32 board reads high when a button is pressed");

int main() {
  printf("board: V2.0 pin map ok\n");
  return 0;
}
