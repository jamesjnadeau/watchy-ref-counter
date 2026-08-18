// ---------------------------------------------------------------------------
// The settings menu.
//
// The shape of this -- a highlighted list, UP and DOWN to move, MENU to
// select, BACK to leave, a set-time screen whose current field blinks, a WiFi
// access point for entering credentials, an NTP sync that reports what it did
// -- follows the reference Watchy firmware, sqfmi/Watchy (MIT licence), by way
// of credit. The code is this project's own; nothing is linked from or copied
// out of that repo.
//
// Deliberate differences from the reference:
//   - no "Show Accelerometer"; nothing on a play clock reads the sensor
//   - colours follow DARK_MODE rather than being fixed black on white
//   - actions never redraw the menu themselves; open() owns that
//   - NTP goes through the ESP32 core's SNTP client, not a separate library
//   - the digits reuse this project's seven-segment drawing, not a font
//   - "TZ" and "DST" have no counterpart there; the reference firmware takes
//     its offset from a compile-time constant
// ---------------------------------------------------------------------------

#include "RefMenu.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "RefPanel.h"
#include "RefZone.h"
#include "board.h"
#include "settings.h"

namespace RefMenu {
namespace {

auto &display = RefPanel::display;

// Two of these read their current value rather than being fixed text, so the
// menu doubles as the status display for the zone and the DST switch.
enum Item : uint8_t {
  ITEM_ABOUT,
  ITEM_BUZZ,
  ITEM_SET_TIME,
  ITEM_ZONE,
  ITEM_DST,
  ITEM_WIFI,
  ITEM_SYNC,
  ITEM_COUNT,
};
const int16_t MENU_ROW_H = 25;

// Longest is "DST: Auto"; the zone names are capped at eight characters so
// "TZ: Mountain" is the widest this can get.
void itemLabel(uint8_t i, char *buf, size_t n) {
  switch (i) {
  case ITEM_ABOUT:    snprintf(buf, n, "About"); break;
  case ITEM_BUZZ:     snprintf(buf, n, "Vibrate Motor"); break;
  case ITEM_SET_TIME: snprintf(buf, n, "Set Time"); break;
  case ITEM_ZONE:     snprintf(buf, n, "TZ: %s", RefZone::name(RefZone::index())); break;
  case ITEM_DST:      snprintf(buf, n, "DST: %s", RefZone::dstAuto() ? "Auto" : "Off"); break;
  case ITEM_WIFI:     snprintf(buf, n, "Setup WiFi"); break;
  case ITEM_SYNC:     snprintf(buf, n, "Sync NTP"); break;
  default:            snprintf(buf, n, "?"); break;
  }
}

// "UTC-5", or "UTC-9:30" if a zone ever needs it.
void formatOffset(char *buf, size_t n, long seconds) {
  const char sign = seconds < 0 ? '-' : '+';
  long mag = seconds < 0 ? -seconds : seconds;
  if (mag > 24L * 3600L) {
    mag = 24L * 3600L; // no real zone is further out, and this bounds the width
  }
  const int hours = (int)(mag / 3600);
  const int mins  = (int)((mag % 3600) / 60);
  if (mins == 0) {
    snprintf(buf, n, "UTC%c%d", sign, hours);
  } else {
    snprintf(buf, n, "UTC%c%d:%02d", sign, hours, mins);
  }
}

enum SetField : int8_t { SET_HOUR, SET_MINUTE, SET_YEAR, SET_MONTH, SET_DAY };

bool pressed(uint8_t pin) { return digitalRead(pin) == BTN_PRESSED_LEVEL; }

void claimButtons() {
  pinMode(PIN_BTN_MENU, INPUT);
  pinMode(PIN_BTN_BACK, INPUT);
  pinMode(PIN_BTN_UP, INPUT);
  pinMode(PIN_BTN_DOWN, INPUT);
}

void beginTextScreen() {
  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(THEME_FG);
  display.setCursor(0, 20);
}

void printTwo(int v) {
  if (v < 10) {
    display.print("0");
  }
  display.print(v);
}

void drawMenu(uint8_t index, bool partial) {
  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);

  char label[24];
  int16_t x1, y1;
  uint16_t w, h;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    itemLabel(i, label, sizeof(label));
    const int16_t yPos = MENU_ROW_H + (MENU_ROW_H * i);
    display.setCursor(0, yPos);
    if (i == index) {
      display.getTextBounds(label, 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, DISPLAY_WIDTH, h + 15, THEME_FG);
      display.setTextColor(THEME_BG);
    } else {
      display.setTextColor(THEME_FG);
    }
    display.println(label);
  }
  display.display(partial);
}

void showAbout(RefClock &refClock) {
  beginTextScreen();

  display.print("Ref Counter ");
  display.println(REF_COUNTER_VERSION);
  display.print("Board: v");
  display.println(refClock.boardRevision() / 10.0f, 1);
  display.print("Batt: ");
  display.print(refClock.batteryVolts(), 2);
  display.println("V");

  struct tm now;
  const bool haveTime = refClock.localNow(now);
  display.print("Now: ");
  if (!haveTime) {
    display.println("not set");
  } else {
    display.print(now.tm_year + 1900);
    display.print("/");
    printTwo(now.tm_mon + 1);
    display.print("/");
    printTwo(now.tm_mday);
    display.print(" ");
    printTwo(now.tm_hour);
    display.print(":");
    printTwo(now.tm_min);
    display.println();
  }

  const time_t nowEpoch = haveTime ? refClock.rtc().epoch() : 0;
  const time_t boot     = refClock.bootEpoch();
  if (boot != 0 && nowEpoch > boot) {
    const uint32_t up = (uint32_t)(nowEpoch - boot);
    display.print("Up: ");
    display.print(up / 86400);
    display.print("d");
    display.print((up % 86400) / 3600);
    display.print("h");
    display.print((up % 3600) / 60);
    display.println("m");
  }

  const time_t sync = refClock.lastSyncEpoch();
  if (sync == 0) {
    display.println("NTP: never");
  } else if (nowEpoch > sync) {
    display.print("NTP: ");
    display.print((uint32_t)(nowEpoch - sync) / 3600);
    display.println("h ago");
  }

  const char *rtcName = "none";
  switch (refClock.rtc().kind()) {
  case RefRtc::DS3231:   rtcName = "DS3231"; break;
  case RefRtc::PCF8563:  rtcName = "PCF8563"; break;
  case RefRtc::INTERNAL: rtcName = "internal"; break;
  default: break;
  }
  display.print("RTC: ");
  display.println(rtcName);

  // The offset actually in force, which is the only way to confirm from the
  // watch that the daylight saving rule did what was expected of it.
  char offset[12];
  formatOffset(offset, sizeof(offset), refClock.utcOffsetSeconds());
  display.print(RefZone::name(RefZone::index()));
  display.print(" ");
  display.print(refClock.zoneAbbrev());
  display.print(" ");
  display.println(offset);

  display.display(false); // full refresh
}

// The zone picker. Eleven zones do not fit on a 200 pixel panel, so this
// scrolls a seven row window and reports the highlighted zone underneath.
void pickTimeZone() {
  const int16_t ROW_H     = 22;
  const uint8_t VISIBLE   = 7;
  const int16_t FIRST_Y   = 22;
  const int16_t RULE_Y    = 164;
  const uint8_t total     = RefZone::count();

  uint8_t index = RefZone::index();
  uint8_t top   = index < VISIBLE ? 0 : (uint8_t)(index - VISIBLE + 1);

  claimButtons();
  display.setFullWindow();

  bool first = true;
  uint32_t lastActivity = millis();
  while (millis() - lastActivity < MENU_TIMEOUT_MS) {
    // Keep the highlighted row inside the visible window.
    if (index < top) {
      top = index;
    } else if (index >= top + VISIBLE) {
      top = (uint8_t)(index - VISIBLE + 1);
    }

    display.fillScreen(THEME_BG);
    display.setFont(&FreeMonoBold9pt7b);

    char row[24], offset[12];
    for (uint8_t slot = 0; slot < VISIBLE && top + slot < total; slot++) {
      const uint8_t z    = (uint8_t)(top + slot);
      const int16_t yPos = FIRST_Y + ROW_H * slot;
      formatOffset(offset, sizeof(offset),
                   RefZone::standardOffsetMinutes(z) * 60L);
      snprintf(row, sizeof(row), "%-8s %s", RefZone::name(z), offset);

      if (z == index) {
        display.fillRect(0, yPos - 17, DISPLAY_WIDTH, ROW_H - 1, THEME_FG);
        display.setTextColor(THEME_BG);
      } else {
        display.setTextColor(THEME_FG);
      }
      display.setCursor(2, yPos);
      display.print(row);
    }

    display.setTextColor(THEME_FG);
    display.drawFastHLine(0, RULE_Y, DISPLAY_WIDTH, THEME_FG);

    // Abbreviations on the left, position in the list on the right. The
    // standard offset is on the row itself, so this is where the reader finds
    // out whether the zone shifts at all.
    char abbrev[20];
    if (RefZone::observesDst(index)) {
      snprintf(abbrev, sizeof(abbrev), "%s/%s", RefZone::standardAbbrev(index),
               RefZone::daylightAbbrev(index));
    } else {
      snprintf(abbrev, sizeof(abbrev), "%s, no DST",
               RefZone::standardAbbrev(index));
    }
    display.setCursor(2, 180);
    display.print(abbrev);

    char counter[10];
    snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(index + 1),
             (unsigned)total);
    // FreeMonoBold9pt7b advances 11 pixels a glyph, so right-aligning is
    // arithmetic rather than a getTextBounds round trip.
    display.setCursor(DISPLAY_WIDTH - 2 - (int16_t)(strlen(counter) * 11), 180);
    display.print(counter);

    display.setCursor(2, 196);
    display.print(RefZone::region(index));

    display.display(!first); // full refresh on the way in, partial to scroll
    first = false;

    // Poll until something happens, so a settled screen is not redrawn on a
    // loop for no reason.
    while (millis() - lastActivity < MENU_TIMEOUT_MS) {
      if (pressed(PIN_BTN_MENU)) {
        RefZone::setIndex(index);
        Buzzer::pulse(BUZZ_CONFIRM_MS);
        Buttons::waitForRelease();
        return;
      }
      if (pressed(PIN_BTN_BACK)) {
        Buttons::waitForRelease();
        return; // leave the stored zone alone
      }
      if (pressed(PIN_BTN_UP)) {
        index = (index == 0) ? (uint8_t)(total - 1) : (uint8_t)(index - 1);
        lastActivity = millis();
        break;
      }
      if (pressed(PIN_BTN_DOWN)) {
        index = (uint8_t)((index + 1) % total);
        lastActivity = millis();
        break;
      }
      delay(BUTTON_POLL_MS);
    }
  }
}

void showBuzz() {
  display.setFullWindow();
  display.fillScreen(THEME_BG);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(THEME_FG);
  display.setCursor(70, 80);
  display.println("Buzz!");
  display.display(false); // full refresh
  Buzzer::pulse(3, BUZZ_SHORT_MS, BUZZ_GAP_MS);
}

// MENU steps forward through the fields and commits past the last one, BACK
// steps back, UP and DOWN change the value under the cursor. The field being
// edited blinks, which is what tells you where you are.
void setTime(RefClock &refClock) {
  struct tm current;
  if (!refClock.localNow(current)) {
    // Nothing sensible on the clock yet, so start from a fixed point rather
    // than from whatever the chip powered up holding.
    current          = {};
    current.tm_year  = 125; // 2025
    current.tm_mon   = 0;
    current.tm_mday  = 1;
  }

  int hour   = current.tm_hour;
  int minute = current.tm_min;
  int year   = (current.tm_year + 1900) % 100;
  int month  = current.tm_mon + 1;
  int day    = current.tm_mday;

  int8_t field = SET_HOUR;
  bool   blink = false;

  claimButtons();
  display.setFullWindow();

  const int16_t pairY = 40;
  const int16_t leftX = 4;
  const int16_t rightX = DISPLAY_WIDTH - RefDisplay::DIGIT_PAIR_W - 4;

  while (true) {
    if (pressed(PIN_BTN_MENU)) {
      field++;
      if (field > SET_DAY) {
        break;
      }
    }
    if (pressed(PIN_BTN_BACK) && field != SET_HOUR) {
      field--;
    }

    blink = !blink;

    const int delta = pressed(PIN_BTN_DOWN) ? 1 : (pressed(PIN_BTN_UP) ? -1 : 0);
    if (delta != 0) {
      blink = true; // never hide the field the user is actively changing
      switch (field) {
      case SET_HOUR:   hour = (hour + delta + 24) % 24; break;
      case SET_MINUTE: minute = (minute + delta + 60) % 60; break;
      case SET_YEAR:   year = (year + delta + 100) % 100; break;
      case SET_MONTH:  month = (month - 1 + delta + 12) % 12 + 1; break;
      case SET_DAY:    day = (day - 1 + delta + 31) % 31 + 1; break;
      default: break;
      }
    }

    display.fillScreen(THEME_BG);
    display.setTextColor(THEME_FG);

    RefDisplay::drawDigitPair(leftX, pairY, (uint8_t)hour,
                              field != SET_HOUR || blink);
    RefDisplay::drawDigitPair(rightX, pairY, (uint8_t)minute,
                              field != SET_MINUTE || blink);
    // Colon between the two pairs.
    display.fillRect(DISPLAY_WIDTH / 2 - 4, pairY + 18, 8, 8, THEME_FG);
    display.fillRect(DISPLAY_WIDTH / 2 - 4, pairY + 40, 8, 8, THEME_FG);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(45, 150);
    display.setTextColor(field == SET_YEAR && !blink ? THEME_BG : THEME_FG);
    display.print(2000 + year);
    display.setTextColor(THEME_FG);
    display.print("/");
    display.setTextColor(field == SET_MONTH && !blink ? THEME_BG : THEME_FG);
    printTwo(month);
    display.setTextColor(THEME_FG);
    display.print("/");
    display.setTextColor(field == SET_DAY && !blink ? THEME_BG : THEME_FG);
    printTwo(day);

    display.setTextColor(THEME_FG);
    display.setCursor(6, 185);
    display.print("MENU next  BACK prev");

    display.display(true); // partial refresh
  }

  struct tm t = {};
  t.tm_year   = 100 + year; // years since 1900
  t.tm_mon    = month - 1;
  t.tm_mday   = day;
  t.tm_hour   = hour;
  t.tm_min    = minute;
  t.tm_sec    = 0;
  // What was typed is local time; the RTC holds UTC, so this converts.
  refClock.setLocal(t);
}

void portalCallback(WiFiManager *) {
  beginTextScreen();
  display.setCursor(0, 30);
  display.println("Connect to");
  display.print("SSID: ");
  display.println(WIFI_AP_NAME);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
  display.println("MAC:");
  display.println(WiFi.softAPmacAddress().c_str());
  display.display(false); // full refresh
}

void setupWifi() {
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(WIFI_AP_TIMEOUT_S);
  wifiManager.setAPCallback(portalCallback);

  beginTextScreen();
  if (!wifiManager.autoConnect(WIFI_AP_NAME)) {
    display.println("Setup failed or");
    display.println("timed out.");
  } else {
    display.println("Connected to:");
    display.println(WiFi.SSID());
    display.println("Local IP:");
    display.println(WiFi.localIP());
  }
  display.display(false); // full refresh

  WiFi.mode(WIFI_OFF);
}

void showSyncNTP(RefClock &refClock) {
  beginTextScreen();
  display.setCursor(0, 30);
  display.println("Syncing NTP...");
  display.print("Zone: ");
  display.println(RefZone::name(RefZone::index()));
  display.display(false); // full refresh

  bool connected = WiFi.begin() != WL_CONNECT_FAILED &&
                   WiFi.waitForConnectResult() == WL_CONNECTED;
  if (!connected) {
    display.println("WiFi not configured");
  } else if (refClock.syncFromNtp()) {
    display.println("Sync OK. Time is:");
    struct tm now;
    if (refClock.localNow(now)) {
      display.print(now.tm_year + 1900);
      display.print("/");
      printTwo(now.tm_mon + 1);
      display.print("/");
      printTwo(now.tm_mday);
      display.print(" ");
      printTwo(now.tm_hour);
      display.print(":");
      printTwo(now.tm_min);
      display.print(" ");
      display.println(refClock.zoneAbbrev());
    }
  } else {
    display.println("Sync failed");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  display.display(true); // partial refresh
  delay(3000);
}

// Hold a result screen until the user backs out of it.
void waitForBack() {
  pinMode(PIN_BTN_BACK, INPUT);
  const uint32_t deadline = millis() + MENU_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    if (pressed(PIN_BTN_BACK)) {
      break;
    }
    delay(BUTTON_POLL_MS);
  }
  Buttons::waitForRelease();
}

} // namespace

void open(RefClock &refClock) {
  // The hold that opened the menu is still down; without this it would land as
  // an immediate selection of the first entry.
  Buttons::waitForRelease();

  uint8_t index = 0;
  drawMenu(index, false);
  claimButtons();

  uint32_t lastActivity = millis();
  while (millis() - lastActivity < MENU_TIMEOUT_MS) {
    if (pressed(PIN_BTN_MENU)) {
      Buttons::waitForRelease();
      bool holdResult = false;
      // The DST switch never leaves the menu, so it gets the quick partial
      // redraw rather than the 2.6s full one every other entry earns.
      bool quickRedraw = false;
      switch (index) {
      case ITEM_ABOUT:    showAbout(refClock); holdResult = true; break;
      case ITEM_BUZZ:     showBuzz(); break;
      case ITEM_SET_TIME: setTime(refClock); break;
      case ITEM_ZONE:     pickTimeZone(); break;
      case ITEM_DST:
        RefZone::setDstAuto(!RefZone::dstAuto());
        Buzzer::pulse(BUZZ_CONFIRM_MS);
        quickRedraw = true;
        break;
      case ITEM_WIFI:     setupWifi(); holdResult = true; break;
      case ITEM_SYNC:     showSyncNTP(refClock); break;
      default: break;
      }
      if (holdResult) {
        waitForBack();
      }
      Buttons::waitForRelease();
      claimButtons();
      drawMenu(index, quickRedraw);
      lastActivity = millis();
    } else if (pressed(PIN_BTN_BACK)) {
      break; // out of the menu entirely
    } else if (pressed(PIN_BTN_UP)) {
      index = (index == 0) ? ITEM_COUNT - 1 : index - 1;
      drawMenu(index, true);
      lastActivity = millis();
    } else if (pressed(PIN_BTN_DOWN)) {
      index = (index + 1) % ITEM_COUNT;
      drawMenu(index, true);
      lastActivity = millis();
    }
  }

  Buttons::waitForRelease();
  // The loop above read the pins raw. Re-seed the debounce state so a button
  // still settling is not mistaken for a fresh press by the main loop.
  Buttons::resync();
}

} // namespace RefMenu
