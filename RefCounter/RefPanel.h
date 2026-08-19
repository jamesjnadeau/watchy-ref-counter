#ifndef REF_PANEL_H
#define REF_PANEL_H

#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD2_BW.h>

#include "board.h"

// The e-paper panel itself.
//
// Watchy fits a 200x200 GDEY0154D67, driven by an SSD1681. GxEPD2 ships a
// driver for exactly that part, including its fast partial update mode, so
// there is nothing here but wiring it to the right pins.
//
// The GDEY replaces the GDEH0154D67, which Good Display has discontinued.
// Same controller, same 200x200, same 24-pin FPC and the same GxEPD2
// constructor, so the swap is the driver class and nothing else.
//
// The reference firmware carries its own lightly tuned fork of this driver.
// This project uses the upstream one so it has no dependency on that repo.
namespace RefPanel {

// One page the full height of the panel, so the whole 200x200 framebuffer is
// in RAM at once. That is what lets a caller repaint one region and refresh
// only that window.
typedef GxEPD2_BW<GxEPD2_154_GDEY0154D67,
                  GxEPD2_154_GDEY0154D67::HEIGHT> Panel;

extern Panel display;

// Starts SPI and resets the panel. Call once from setup().
void begin();

} // namespace RefPanel

#endif // REF_PANEL_H
