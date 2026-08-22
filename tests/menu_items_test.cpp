// Host test for the settings menu's row list in RefMenuItems.cpp. Compiles the
// real source, so the order under test is the shipped order.
//
// RefMenu.cpp itself cannot be built here -- GxEPD2, WiFiManager and GPIO --
// which is exactly why the list lives in its own file: the order, the two
// conditional sync rows and the slot arithmetic behind them are cheap to
// check here and expensive to check on a watch.
#include "RefMenuItems.h"

#include "RefSport.h"
#include "RefZone.h"

#include <Preferences.h>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expectEq(const char *what, long got, long want) {
  if (got != want) {
    printf("FAIL %-46s want %ld got %ld\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %ld\n", what, got);
  }
}

static void expectStr(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s want %s got %s\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-46s %s\n", what, got);
  }
}

// The order the list must come back in, for each combination of the two
// conditional rows. Neither row is at the end, so leaving one out moves
// everything below it -- which is the whole reason the menu tracks the
// highlight by item rather than by slot.
static const uint8_t WANT_BARE[] = {
    RefMenu::ITEM_ABOUT,    RefMenu::ITEM_ZONE, RefMenu::ITEM_DST,
    RefMenu::ITEM_SET_TIME, RefMenu::ITEM_WIFI, RefMenu::ITEM_SPORT,
    RefMenu::ITEM_EDIT_CUSTOM};

static const uint8_t WANT_WIFI_ONLY[] = {
    RefMenu::ITEM_ABOUT, RefMenu::ITEM_SYNC,     RefMenu::ITEM_ZONE,
    RefMenu::ITEM_DST,   RefMenu::ITEM_SET_TIME, RefMenu::ITEM_WIFI,
    RefMenu::ITEM_SPORT, RefMenu::ITEM_EDIT_CUSTOM};

static const uint8_t WANT_BT_ONLY[] = {
    RefMenu::ITEM_ABOUT, RefMenu::ITEM_SYNC_BT,  RefMenu::ITEM_ZONE,
    RefMenu::ITEM_DST,   RefMenu::ITEM_SET_TIME, RefMenu::ITEM_WIFI,
    RefMenu::ITEM_SPORT, RefMenu::ITEM_EDIT_CUSTOM};

static const uint8_t WANT_BOTH[] = {
    RefMenu::ITEM_ABOUT,    RefMenu::ITEM_SYNC, RefMenu::ITEM_SYNC_BT,
    RefMenu::ITEM_ZONE,     RefMenu::ITEM_DST,  RefMenu::ITEM_SET_TIME,
    RefMenu::ITEM_WIFI,     RefMenu::ITEM_SPORT, RefMenu::ITEM_EDIT_CUSTOM};

static void expectOrder(const char *what, const uint8_t *got, uint8_t gotCount,
                        const uint8_t *want, uint8_t wantCount) {
  char label[80];
  snprintf(label, sizeof(label), "%s row count", what);
  expectEq(label, gotCount, wantCount);
  const uint8_t n = gotCount < wantCount ? gotCount : wantCount;
  for (uint8_t i = 0; i < n; i++) {
    snprintf(label, sizeof(label), "%s slot %u", what, (unsigned)i);
    expectEq(label, got[i], want[i]);
  }
}

int main() {
  // Nothing stored, so the labels that read a value read the settings.h
  // defaults: Eastern, DST auto, Football.
  PreferencesStub::enable(false);
  RefZone::begin();
  RefSport::begin();

  uint8_t visible[RefMenu::ITEM_COUNT];

  // Neither radio on offer: seven rows, which is exactly what the panel fits
  // without scrolling. No Sync NTP, because with no credentials stored that
  // row could only ever fail, and no Sync BT, because this build has no BLE.
  uint8_t n = RefMenu::buildVisible(false, false, visible);
  expectOrder("bare", visible, n, WANT_BARE,
              (uint8_t)(sizeof(WANT_BARE) / sizeof(WANT_BARE[0])));
  expectEq("sync absent without wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC), -1);
  expectEq("bt absent when unsupported",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC_BT), -1);
  expectEq("setup wifi sits at slot 4",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_WIFI), 4);

  // WiFi set up, no Bluetooth: eight rows, Sync NTP second.
  n = RefMenu::buildVisible(true, false, visible);
  expectOrder("wifi only", visible, n, WANT_WIFI_ONLY,
              (uint8_t)(sizeof(WANT_WIFI_ONLY) / sizeof(WANT_WIFI_ONLY[0])));
  expectEq("sync is second with wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC), 1);
  // The case the menu depends on: running Setup WiFi inserts a row *above*
  // it, so the highlight has to be moved by item, not left on slot 4.
  expectEq("setup wifi shifts down one",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_WIFI), 5);
  expectEq("unknown item has no slot",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_COUNT), -1);

  // Bluetooth but no WiFi. This is the interesting one: Sync BT needs nothing
  // saved, so it is there from the first boot, and it takes the slot Sync NTP
  // would otherwise hold.
  n = RefMenu::buildVisible(false, true, visible);
  expectOrder("bt only", visible, n, WANT_BT_ONLY,
              (uint8_t)(sizeof(WANT_BT_ONLY) / sizeof(WANT_BT_ONLY[0])));
  expectEq("bt is second without wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC_BT), 1);
  expectEq("sync still absent without wifi",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC), -1);

  // Both: nine rows, the two sync rows together at the top, and the list is
  // now two longer than the seven-row window the menu scrolls.
  n = RefMenu::buildVisible(true, true, visible);
  expectOrder("both", visible, n, WANT_BOTH,
              (uint8_t)(sizeof(WANT_BOTH) / sizeof(WANT_BOTH[0])));
  expectEq("sync is second", RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC),
           1);
  expectEq("bt is third", RefMenu::slotOf(visible, n, RefMenu::ITEM_SYNC_BT),
           2);
  expectEq("setup wifi shifts down two",
           RefMenu::slotOf(visible, n, RefMenu::ITEM_WIFI), 6);

  // Labels. Fixed text where it is fixed, and the three value-carrying rows
  // reading their defaults.
  char label[RefMenu::ITEM_LABEL_MAX];
  RefMenu::itemLabel(RefMenu::ITEM_ABOUT, label, sizeof(label));
  expectStr("about label", label, "About");
  RefMenu::itemLabel(RefMenu::ITEM_SYNC, label, sizeof(label));
  expectStr("sync label", label, "Sync NTP");
  RefMenu::itemLabel(RefMenu::ITEM_SYNC_BT, label, sizeof(label));
  expectStr("bt sync label", label, "Sync BT");
  RefMenu::itemLabel(RefMenu::ITEM_SET_TIME, label, sizeof(label));
  expectStr("set time label", label, "Set Time");
  RefMenu::itemLabel(RefMenu::ITEM_WIFI, label, sizeof(label));
  expectStr("setup wifi label", label, "Setup WiFi");
  RefMenu::itemLabel(RefMenu::ITEM_EDIT_CUSTOM, label, sizeof(label));
  expectStr("edit custom label", label, "Edit Custom");
  RefMenu::itemLabel(RefMenu::ITEM_ZONE, label, sizeof(label));
  expectStr("zone label reads the zone", label, "TZ: Eastern");
  RefMenu::itemLabel(RefMenu::ITEM_DST, label, sizeof(label));
  expectStr("dst label reads the switch", label, "DST: Auto");
  RefMenu::itemLabel(RefMenu::ITEM_SPORT, label, sizeof(label));
  expectStr("sport label reads the sport", label, "Sport: Football");

  // Every row has to fit the panel: FreeMonoBold9pt7b advances 11 pixels a
  // glyph and the panel is 200 wide, so 18 glyphs is the ceiling. The buzz
  // test is gone, and must not creep back in.
  for (uint8_t i = 0; i < RefMenu::ITEM_COUNT; i++) {
    RefMenu::itemLabel(i, label, sizeof(label));
    char what[80];
    snprintf(what, sizeof(what), "%s fits 18 glyphs", label);
    expectEq(what, (long)strlen(label) <= 18, 1);
    snprintf(what, sizeof(what), "%s is not the buzz test", label);
    expectEq(what, strcmp(label, "Vibrate Motor") != 0, 1);
  }

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
