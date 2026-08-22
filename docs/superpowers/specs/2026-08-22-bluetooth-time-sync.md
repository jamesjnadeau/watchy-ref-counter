# Getting the Time From a Phone Over Bluetooth

**Date:** 2026-08-22
**Status:** implemented

## What is being asked for

A second way to set the watch's clock from an outside source: a phone, over
Bluetooth, alongside the existing WiFi/NTP sync rather than replacing it.

## Decisions taken

**Which way round the exchange goes.** The watch is the **peripheral** and the
phone **writes** the time to it. The alternative — the watch scanning as a
central and reading the time out of the phone — does not work: neither iOS nor
Android hands its clock to an arbitrary peripheral that asks for it. Writing
into a watch's Current Time Service is exactly what that service is for, and
apps that already do it (Gadgetbridge automatically, nRF Connect by hand) then
work with no app written for this watch. It is the route the open source
PineTime firmware takes.

**Which characteristics.** The two standard ones, no custom service:

| UUID | Direction | What it carries |
| --- | --- | --- |
| `0x2A2B` Current Time | read + write | year, month, day, hour, minute, second |
| `0x2A0F` Local Time Information | read + write | the UTC offset that clock is in |

Both are readable as well as writable, so a phone that reads sees the watch's
own time rather than an empty value. Advertised service UUID is `0x1805`.

**How a wall clock becomes UTC.** The RTC holds UTC and the zone is applied on
the way out, so a written time has to be converted. Two cases, and the result
screen says which one it took:

- The phone also wrote Local Time Information → its offset is used, exactly,
  and the watch's own **TZ** setting does not enter into it.
- It did not → the time is taken as local to the zone the watch is set to,
  which is the same assumption **Set Time** makes.

**What is refused.** A write is rejected outright — the clock is left alone —
unless it is at least the 7 byte date-and-time part, and every field is in
range, and the day exists in that month of that year. The year is held to
2020–2099: the RTC chips hold two BCD digits so nothing else is representable
anyway, and the low end throws out the one plausible wrong write, a phone
whose own clock has never been set.

**Ordering between the two writes.** A client writes them in either order. A
Current Time that arrives with no zone beside it is held for 400ms before it
is acted on, in case the zone is still on its way; one that arrives after a
zone is acted on at once.

**Security.** None. No bonding, no encryption, no pairing. The only thing a
stranger in radio range can do is set the clock, and only during the minute
the sync screen is open. Pairing every phone that might set the time would
cost more than that is worth.

**When the radio is up.** Only inside a sync window. `RefBleTime::begin()`
starts the controller, `end()` shuts it down. WiFi is dropped before Bluetooth
starts, so the two are never up together.

**Automatic sync.** Stays with NTP by default. `BT_AUTO_SYNC_SECONDS` (0 by
default) lets a due sync that NTP could not satisfy fall back to a Bluetooth
window of that length — worth having only where a companion app syncs time to
the watch by itself, since advertising to nobody costs battery.

**Menu.** A new `Sync BT` row, directly under `Sync NTP`. Unlike that one it
needs nothing saved, so it is offered from the first boot; it disappears only
in a build with `BT_TIME_SYNC` off or no BLE at all. Nine rows now, scrolled
through the same seven-row window.

## Shape of the code

Three files, split along the line the rest of the project already uses —
anything testable on a host is kept out of the file that touches hardware.

| File | What it owns |
| --- | --- |
| `RefCtsTime.h/.cpp` | the byte layouts and their range checks; no Arduino headers, host-tested |
| `RefBleTime.h/.cpp` | the GATT server, advertising, and the radio's lifetime |
| `RefClock` (existing) | converting a received stamp into the RTC's UTC, and the sync bookkeeping |

`RefMenu` owns the screen: it runs its own wait loop so it can redraw when a
phone connects and take BACK for an answer, and calls `RefClock::applyBleStamp`
with what arrives.

The About screen's last-sync line now names the source (`Sync: 5h ago BT`),
because an hour out after a Bluetooth sync is a different fault from a stale
NTP one.

## Portability of the GATT calls

The Arduino-ESP32 BLE library has three callback shapes across the versions
this repo builds against: 2.x, 3.x on Bluedroid, and 3.x on NimBLE. Only the
one-argument `onWrite(BLECharacteristic*)` and `onDisconnect(BLEServer*)`
forms exist in all three, and the newer forms' default implementations fall
through to them, so those are the only ones overridden. One backend delivers
both forms for the same write, so the write handler is written to survive
running twice for one packet.

Connections are counted by `BLEServer::getConnectedCount()` rather than by
tallying callbacks, for the same reason.

`BLEDevice::deinit(true)` is deliberately **not** used: it releases the
controller's memory permanently *and* leaves the library's initialised flag
set, so a second `begin()` in the same power cycle would quietly advertise to
nobody. `deinit(false)` shuts the controller down and lets it come back.

## Not done

- No bonding or encryption, per the decision above.
- No notification of the Current Time characteristic; nothing subscribes.
- No custom "sync time" app for the phone. The standard service is the point.
- Not tested against a phone, and not compiled: the environment this was
  written in could not fetch the ESP32 toolchain. Host tests cover the byte
  layouts and the menu row list.
