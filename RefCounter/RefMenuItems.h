#ifndef REF_MENU_ITEMS_H
#define REF_MENU_ITEMS_H

#include <stddef.h>
#include <stdint.h>

// The settings menu's row list: which rows exist, what order they come in, and
// what each one says.
//
// Split out of RefMenu.cpp so it can be compiled and tested on a host --
// RefMenu.cpp pulls in GxEPD2, WiFiManager and the GPIO layer, none of which
// build off the watch. Nothing here draws anything or reads a button.
namespace RefMenu {

// Menu order. About first, because that is the row the menu opens on, then the
// three rows that touch the clock and the two that touch the radio. The sport
// rows are last: they are set once for the season, where everything above them
// is either read or adjusted in the field.
enum Item : uint8_t {
  ITEM_ABOUT,
  ITEM_SYNC,
  ITEM_ZONE,
  ITEM_DST,
  ITEM_SET_TIME,
  ITEM_WIFI,
  ITEM_SPORT,
  ITEM_EDIT_CUSTOM,
  ITEM_COUNT,
};

// The widest label is "Sport: Base NCAA" at 16 glyphs, and the panel fits 18.
// 24 leaves room rather than sizing the buffer to the exact width and hoping
// nothing grows past it.
static const size_t ITEM_LABEL_MAX = 24;

// Fill `out` -- which must hold ITEM_COUNT entries -- with the items to show,
// in menu order, and return how many there are. "Sync NTP" is left out until
// WiFi credentials have been saved: with none stored it could only ever fail.
uint8_t buildVisible(bool wifiConfigured, uint8_t *out);

// Which slot of a list built by buildVisible holds `item`, or -1 when that
// item is not currently shown. This is how the highlight follows a row across
// a rebuild: running "Setup WiFi" can make "Sync NTP" appear *above* it, which
// moves it and every row below it down one.
int8_t slotOf(const uint8_t *visible, uint8_t count, uint8_t item);

// The row's text. Three rows read their current value rather than being fixed
// text, so the menu doubles as the status display for the sport, the zone and
// the DST switch.
void itemLabel(uint8_t item, char *buf, size_t n);

} // namespace RefMenu

#endif // REF_MENU_ITEMS_H
