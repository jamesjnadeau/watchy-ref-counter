# Settings Menu Order, the WiFi Gate and Dropping the Buzz Test

**Date:** 2026-08-18
**Status:** agreed, ready to implement

## What is being asked for

Three changes to the on-watch settings menu (`RefCounter/RefMenu.cpp`):

1. **Reorder the rows.** The requested order is About, Sync NTP, TZ, DST, Set
   Time, Setup WiFi.
2. **Hide "Sync NTP" until WiFi has been set up.** With no credentials stored
   the row can only ever fail, so it should not be offered.
3. **Remove the "Vibrate Motor" row** and the buzz-test screen behind it.

## Decisions taken

**Where the two sport rows go.** The requested list names six rows; the menu
also carries `Sport: <name>` and `Edit Custom`. Both stay, at the **bottom** of
the list, so the six requested rows appear in exactly the order given, at the
top. Final order:

| Slot | Row | Shown when |
| --- | --- | --- |
| 1 | `About` | always |
| 2 | `Sync NTP` | only once WiFi has been set up |
| 3 | `TZ: <name>` | always |
| 4 | `DST: Auto/Off` | always |
| 5 | `Set Time` | always |
| 6 | `Setup WiFi` | always |
| 7 | `Sport: <name>` | always |
| 8 | `Edit Custom` | always |

Seven rows when WiFi has never been set up, eight once it has. `MENU_VISIBLE`
is 7, so the menu scrolls only in the eight-row case.

**How "WiFi has been setup" is decided.** A persisted flag in NVS, owned by a
new `RefWifi` module, following the shape of `RefZone` and `RefSport`: one
value cached in RAM, loaded once by `begin()`, written through on change.

- `setupWifi()` sets the flag true when `WiFiManager::autoConnect` succeeds and
  false when it fails or times out. Clearing on failure is correct rather than
  merely tidy: that screen calls `resetSettings()` before it raises its access
  point, so a failed setup really has left the watch with no credentials.
- Reading a cached bool costs nothing. The alternative — asking the ESP32 for
  its stored STA SSID — requires powering up the WiFi driver, which would run
  every time the menu opened or scrolled. Rejected on power grounds.

**Consequence, accepted:** a watch that already has credentials saved from
before this change has no flag stored, so `Sync NTP` is hidden until **Setup
WiFi** is run once more. This must be called out in the README.

**Removing the buzz test.** `showBuzz()` and the `ITEM_BUZZ` row go entirely.
`Buzzer` itself stays — the confirmation pulse, the warning marks and the
expiry buzz all still use it.

## Design

The menu's row list is pulled out of `RefMenu.cpp` into a new pure
`RefMenuItems` translation unit — the order, the visibility rule and the row
labels. `RefMenu.cpp` cannot be compiled on a host (GxEPD2, WiFiManager, GPIO),
and this logic is exactly the sort that is expensive to check on hardware. The
split follows the precedent set by `RefSegments`, which was carved out of
`RefDisplay.cpp` for the same reason.

```
RefMenuItems.h/.cpp   enum Item, buildVisible(), slotOf(), itemLabel()   host-testable
RefWifi.h/.cpp        begin(), configured(), setConfigured()              host-testable
RefMenu.cpp           drawing and button handling only                    watch only
```

`RefMenu.cpp` keeps an array of visible item ids and a count, and its
highlight index becomes a **slot** index into that array rather than an item
id. Wrapping and scrolling use the visible count.

**The one non-obvious case:** running **Setup WiFi** can make **Sync NTP**
appear above it, which shifts `Setup WiFi` and everything below it down a slot.
So after every action the list is rebuilt and the highlight is moved to
`slotOf(<the item the user just ran>)`, not left on its old slot number.

## Out of scope

- Gating the automatic NTP resync (`RefClock`) on the same flag. It is already
  off by default (`NTP_RESYNC_HOURS = 0`) and the ask is about the menu.
- Any change to what the six retained screens do once opened.
- Hardware verification. This firmware has never run on a watch; see the
  README's *Known limitations*.
