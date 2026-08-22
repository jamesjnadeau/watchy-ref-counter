#include "RefMenuItems.h"

#include <stdio.h>

#include "RefSport.h"
#include "RefZone.h"

namespace RefMenu {

uint8_t buildVisible(bool wifiConfigured, bool btAvailable, uint8_t *out) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    if (i == ITEM_SYNC && !wifiConfigured) {
      continue;
    }
    if (i == ITEM_SYNC_BT && !btAvailable) {
      continue;
    }
    out[count++] = i;
  }
  return count;
}

int8_t slotOf(const uint8_t *visible, uint8_t count, uint8_t item) {
  for (uint8_t s = 0; s < count; s++) {
    if (visible[s] == item) {
      return (int8_t)s;
    }
  }
  return -1;
}

void itemLabel(uint8_t item, char *buf, size_t n) {
  switch (item) {
  case ITEM_ABOUT:    snprintf(buf, n, "About"); break;
  case ITEM_SYNC:     snprintf(buf, n, "Sync NTP"); break;
  case ITEM_SYNC_BT:  snprintf(buf, n, "Sync BT"); break;
  case ITEM_ZONE:     snprintf(buf, n, "TZ: %s", RefZone::name(RefZone::index())); break;
  case ITEM_DST:      snprintf(buf, n, "DST: %s", RefZone::dstAuto() ? "Auto" : "Off"); break;
  case ITEM_SET_TIME: snprintf(buf, n, "Set Time"); break;
  case ITEM_WIFI:     snprintf(buf, n, "Setup WiFi"); break;
  case ITEM_SPORT:    snprintf(buf, n, "Sport: %s", RefSport::active().name); break;
  case ITEM_EDIT_CUSTOM: snprintf(buf, n, "Edit Custom"); break;
  default:            snprintf(buf, n, "?"); break;
  }
}

} // namespace RefMenu
