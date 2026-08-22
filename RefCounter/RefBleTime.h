#ifndef REF_BLE_TIME_H
#define REF_BLE_TIME_H

#include "RefCtsTime.h"

// Whether this build has a Bluetooth stack to talk to at all.
//
// Every ESP32 in this project's board list has BLE, but the check costs
// nothing and it keeps the sketch buildable against a core or a target
// configured without Bluetooth: with the macro at 0 the rest of this file
// compiles to stubs that report "not supported", the menu leaves the row out,
// and no BLE code is linked in.
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_ENABLED) && __has_include(<BLEDevice.h>)
#define REF_BLE_AVAILABLE 1
#else
#define REF_BLE_AVAILABLE 0
#endif

// Getting the time from a phone over Bluetooth Low Energy.
//
// The watch is the peripheral. It advertises the standard Current Time
// Service (0x1805) and waits; the phone connects and *writes* the time into
// it. That direction is deliberate, and it is the one that works without an
// app written for this watch:
//
//   - iOS and Android both refuse to hand their clock to an arbitrary
//     peripheral that asks for it. Nothing the watch can do as a central
//     gets the time out of a stock phone.
//   - Writing into a watch's Current Time Service is, by contrast, exactly
//     what the service is for, and existing apps already do it -- Gadgetbridge
//     syncs time to a connected device this way, and nRF Connect can write the
//     characteristic by hand. That is the same route the open source PineTime
//     firmware takes.
//
// Nothing here is bonded or encrypted. The only thing a stranger in radio
// range can do is set the watch's clock, the watch advertises only while this
// screen is open, and pairing every phone that might set it would cost more
// than that is worth.
//
// The radio is only ever up while a sync window is open: begin() brings the
// controller up, end() takes it back down and releases its memory. WiFi is
// never up at the same time -- RefClock drops the radio before it opens a
// window.
namespace RefBleTime {

// False when the build has no BLE, or when BT_TIME_SYNC is off in settings.h.
// The menu asks this before offering the row.
bool supported();

// Bring the radio up and start advertising. False if BLE is unavailable or
// the stack refused to start; there is nothing to end() in that case.
bool begin();

// Publish the watch's own time, so a phone that reads the characteristics
// sees something useful rather than an empty value. `local` is local wall
// time and `offsetSeconds` the offset it is expressed in -- the same pair the
// watch would write into an RTC. Optional; skip it when the clock is unset.
void publish(const struct tm &local, long offsetSeconds);

// Whether a phone is connected right now. Only for the progress screen: a
// connection is not a sync, and plenty of phones connect, look around and
// leave without writing anything.
bool connected();

// Hand over a time a phone has written, if one has arrived and settled.
// Consumes it, so a second call returns false until the next write.
//
// "Settled" is worth a word. A client that writes both characteristics writes
// them in either order, so a Current Time that arrives without a Local Time
// Information beside it is held for a moment before being reported, in case
// the zone is still on its way. The wait is under half a second and only
// applies when no zone has been seen at all.
bool take(RefCtsTime::Stamp &out);

// Stop advertising, drop any connection, and release the controller's memory.
// Safe to call whether or not begin() succeeded.
void end();

// The name the watch advertises under, for the screen to show. Comes from
// BT_DEVICE_NAME in settings.h.
const char *deviceName();

} // namespace RefBleTime

#endif // REF_BLE_TIME_H
