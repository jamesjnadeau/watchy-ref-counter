// The C6 board's pin map, asserted. Every number here comes from
// board-files/elec/src/watchy.ato by way of board.h; board.h's own comment
// notes that the netlist half is checked by board-files/checks and this half
// was checked by nothing. Now it is checked by this.

#define ARDUINO_ESP32C6_DEV // as platformio.ini's watchy_c6 env does

#include <stdio.h>
#include <string.h>

#include "board.h"

static_assert(PIN_I2C_SDA == 7 && PIN_I2C_SCL == 6, "C6 I2C pins");
static_assert(PIN_SPI_SCK == 15 && PIN_SPI_MISO == 23 && PIN_SPI_MOSI == 14 &&
                  PIN_SPI_SS == 8,
              "C6 SPI pins; MISO is a spare, the panel is write-only");
static_assert(PIN_BTN_MENU == 0 && PIN_BTN_BACK == 1 && PIN_BTN_UP == 2 &&
                  PIN_BTN_DOWN == 3,
              "C6 buttons are IO0-IO3, all LP GPIOs so one ext1 mask covers "
              "them");
static_assert(PIN_DISPLAY_CS == 8 && PIN_DISPLAY_DC == 18 &&
                  PIN_DISPLAY_RST == 19 && PIN_DISPLAY_BUSY == 20,
              "C6 panel control pins");
static_assert(PIN_VIB_MOTOR == 21, "C6 vibration motor");
static_assert(PIN_BATT_ADC == 5, "C6 battery tap");
static_assert(BTN_PRESSED_LEVEL == LOW,
              "C6 buttons pull their pin low when pressed");

int main() {
  if (strcmp(BOARD_NAME, "C6") != 0) {
    printf("FAIL: BOARD_NAME is \"%s\", want \"C6\"\n", BOARD_NAME);
    return 1;
  }
  printf("board: C6 pin map and name ok\n");
  return 0;
}
