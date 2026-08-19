#include "RefPanel.h"

#include <SPI.h>

namespace RefPanel {

Panel display(GxEPD2_154_GDEY0154D67(PIN_DISPLAY_CS, PIN_DISPLAY_DC,
                                     PIN_DISPLAY_RST, PIN_DISPLAY_BUSY));

void begin() {
#ifdef ARDUINO_ESP32C6_DEV
  // The C6 has no default pin assignment for this bus, so it has to be
  // spelled out before the panel is touched. This block used to be ESP32-S3
  // only, which left the C6 -- whose SPI pins are just as non-default --
  // talking to the panel over whatever the core picked.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SS);
#endif
  // No serial diagnostics, 10ms reset pulse, and let GxEPD2 drive the reset
  // line as a normal output.
  display.init(0, true, 10, false);
}

} // namespace RefPanel
