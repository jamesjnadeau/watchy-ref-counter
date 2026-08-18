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
// ---------------------------------------------------------------------------

#include "RefMenu.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "Buttons.h"
#include "Buzzer.h"
#include "RefDisplay.h"
#include "RefPanel.h"
#include "board.h"
#include "settings.h"

namespace RefMenu {
namespace {

auto &display = RefPanel::display;

const char *const ITEMS[] = {
    "About", "Vibrate Motor", "Set Time", "Setup WiFi", "Sync NTP",
};
const uint8_t ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);
const int16_t MENU_ROW_H = 25;

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

  int16_t x1, y1;
  uint16_t w, h;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    const int16_t yPos = MENU_ROW_H + (MENU_ROW_H * i);
    display.setCursor(0, yPos);
    if (i == index) {
      display.getTextBounds(ITEMS[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, DISPLAY_WIDTH, h + 15, THEME_FG);
      display.setTextColor(THEME_BG);
    } else {
      display.setTextColor(THEME_FG);
    }
    display.println(ITEMS[i]);
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
  const bool haveTime = refClock.rtc().read(now);
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

  display.display(false); // full refresh
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
  if (!refClock.rtc().read(current)) {
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
  refClock.rtc().set(t);
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
  display.print("GMT offset: ");
  display.println(GMT_OFFSET_SECONDS);
  display.display(false); // full refresh

  bool connected = WiFi.begin() != WL_CONNECT_FAILED &&
                   WiFi.waitForConnectResult() == WL_CONNECTED;
  if (!connected) {
    display.println("WiFi not configured");
  } else if (refClock.syncFromNtp()) {
    display.println("Sync OK. Time is:");
    struct tm now;
    if (refClock.rtc().read(now)) {
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
      switch (index) {
      case 0: showAbout(refClock); holdResult = true; break;
      case 1: showBuzz(); break;
      case 2: setTime(refClock); break;
      case 3: setupWifi(); holdResult = true; break;
      case 4: showSyncNTP(refClock); break;
      default: break;
      }
      if (holdResult) {
        waitForBack();
      }
      Buttons::waitForRelease();
      claimButtons();
      drawMenu(index, false);
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
  Buttons::begin();
}

} // namespace RefMenu
