// The Bluetooth Low Energy side of getting the time from a phone. The byte
// layouts it hands around are in RefCtsTime; why the watch is the peripheral
// and the phone does the writing is in RefBleTime.h.
#include "RefBleTime.h"

#include "settings.h"

#if REF_BLE_AVAILABLE

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string.h>

namespace RefBleTime {
namespace {

// Assigned numbers, from the Bluetooth SIG.
const uint16_t UUID_CURRENT_TIME_SERVICE = 0x1805;
const uint16_t UUID_CURRENT_TIME         = 0x2A2B;
const uint16_t UUID_LOCAL_TIME_INFO      = 0x2A0F;

// Cosmetic: the icon and label a phone shows beside the name in a scan list.
const uint16_t APPEARANCE_SPORTS_WATCH = 0x00C1;

// How long a Current Time write with no zone beside it is held back, in case
// the client is writing Local Time Information as well and got there second.
// A client that writes the zone first is not delayed at all.
const uint32_t ZONE_GRACE_MS = 400;

BLEServer         *server        = nullptr;
BLECharacteristic *currentTime   = nullptr;
BLECharacteristic *localTimeInfo = nullptr;
bool               running       = false;

// The BLE stack runs its callbacks on its own task, so everything that
// crosses between it and the sketch is touched inside this spinlock. The
// callbacks are task context, not interrupt context, so the plain
// portENTER_CRITICAL is the right one.
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;

struct tm pendingWall;
uint32_t  pendingAt          = 0;
bool      pendingTime        = false;
bool      pendingOffsetKnown = false;
long      pendingOffset      = 0;

void clearPending() {
  portENTER_CRITICAL(&stateLock);
  memset(&pendingWall, 0, sizeof(pendingWall));
  pendingAt          = 0;
  pendingTime        = false;
  pendingOffsetKnown = false;
  pendingOffset      = 0;
  portEXIT_CRITICAL(&stateLock);
}

// Only the one-argument onWrite is overridden. The library grew a second form
// carrying the raw GATT parameters, and then a third when a NimBLE backend
// was added, but the base class's newer forms all fall through to this one
// when it is the only one implemented -- so this builds and runs the same
// against Arduino-ESP32 2.x, 3.x Bluedroid and 3.x NimBLE. Nothing here wants
// the connection details anyway.
//
// One backend calls both forms for the same write, so this can run twice for
// one packet. It is written to survive that: a second pass stores the same
// values over the top of the first.
class TimeCallbacks : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic *c) override {
    const uint8_t *data = c->getData();
    const size_t   len  = c->getLength();
    if (data == nullptr) {
      return;
    }

    if (c == currentTime) {
      struct tm wall;
      // A write that does not parse is dropped on the floor. There is no
      // sensible way to refuse it -- the characteristic has already taken the
      // value by the time this runs -- and the screen showing "waiting"
      // simply goes on waiting, which is the honest report of what happened.
      if (!RefCtsTime::parseCurrentTime(data, len, wall)) {
        return;
      }
      const uint32_t at = millis();
      portENTER_CRITICAL(&stateLock);
      pendingWall = wall;
      pendingAt   = at;
      pendingTime = true;
      portEXIT_CRITICAL(&stateLock);
    } else if (c == localTimeInfo) {
      long offset;
      if (!RefCtsTime::parseLocalTimeInfo(data, len, offset)) {
        return;
      }
      portENTER_CRITICAL(&stateLock);
      pendingOffset      = offset;
      pendingOffsetKnown = true;
      portEXIT_CRITICAL(&stateLock);
    }
  }
};

// Advertising stops the moment a phone connects, and the library does not
// bring it back by itself. A phone that connects, thinks better of it and
// drops off must not end the window, so it is restarted here: the next
// attempt -- or the next phone -- can still find the watch while the screen
// is up. Counting connections is left to the library, which does it already.
class ConnectionCallbacks : public BLEServerCallbacks {
public:
  void onDisconnect(BLEServer *) override {
    if (running) {
      BLEDevice::startAdvertising();
    }
  }
};

// Function-local statics rather than globals: they have to outlive every
// begin()/end() cycle, since the stack keeps the pointers.
TimeCallbacks &timeCallbacks() {
  static TimeCallbacks cb;
  return cb;
}

ConnectionCallbacks &connectionCallbacks() {
  static ConnectionCallbacks cb;
  return cb;
}

} // namespace

bool supported() { return BT_TIME_SYNC; }

const char *deviceName() { return BT_DEVICE_NAME; }

bool begin() {
  if (!supported()) {
    return false;
  }
  if (running) {
    return true;
  }
  clearPending();

  BLEDevice::init(BT_DEVICE_NAME);
  server = BLEDevice::createServer();
  if (server == nullptr) {
    BLEDevice::deinit(false);
    return false;
  }
  server->setCallbacks(&connectionCallbacks());

  BLEService *service =
      server->createService(BLEUUID((uint16_t)UUID_CURRENT_TIME_SERVICE));
  if (service == nullptr) {
    server = nullptr;
    BLEDevice::deinit(false);
    return false;
  }

  // Write-with-response and write-without-response both, because clients
  // differ on which they use and neither costs anything to accept.
  const uint32_t writable = BLECharacteristic::PROPERTY_READ |
                            BLECharacteristic::PROPERTY_WRITE |
                            BLECharacteristic::PROPERTY_WRITE_NR;
  currentTime = service->createCharacteristic(
      BLEUUID((uint16_t)UUID_CURRENT_TIME), writable);
  localTimeInfo = service->createCharacteristic(
      BLEUUID((uint16_t)UUID_LOCAL_TIME_INFO), writable);
  if (currentTime == nullptr || localTimeInfo == nullptr) {
    currentTime   = nullptr;
    localTimeInfo = nullptr;
    server        = nullptr;
    BLEDevice::deinit(false);
    return false;
  }
  currentTime->setCallbacks(&timeCallbacks());
  localTimeInfo->setCallbacks(&timeCallbacks());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLEUUID((uint16_t)UUID_CURRENT_TIME_SERVICE));
  advertising->setAppearance(APPEARANCE_SPORTS_WATCH);
  // Asking for a scan response moves the name out of the advertising packet
  // and into that second one, which leaves the first with room for the
  // service UUID and the appearance. Phones ask for the scan response as a
  // matter of course, so the name still shows up in the list.
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  running = true;
  return true;
}

void publish(const struct tm &local, long offsetSeconds) {
  if (!running || currentTime == nullptr || localTimeInfo == nullptr) {
    return;
  }
  uint8_t buf[RefCtsTime::CURRENT_TIME_LEN];
  // Adjust reason "manual": the watch's own clock was last set by hand or by
  // one of its own syncs, not by whoever is reading it now.
  if (RefCtsTime::encodeCurrentTime(local, RefCtsTime::ADJUST_MANUAL, buf,
                                    sizeof(buf)) != 0) {
    currentTime->setValue(buf, RefCtsTime::CURRENT_TIME_LEN);
  }
  uint8_t zone[RefCtsTime::LOCAL_TIME_INFO_LEN];
  if (RefCtsTime::encodeLocalTimeInfo(offsetSeconds, zone, sizeof(zone)) != 0) {
    localTimeInfo->setValue(zone, RefCtsTime::LOCAL_TIME_INFO_LEN);
  }
}

bool connected() {
  if (!running || server == nullptr) {
    return false;
  }
  // The library's own count, rather than one kept here off the connect and
  // disconnect callbacks: those are delivered in more than one form depending
  // on the backend, and a count that drifted would leave the screen claiming
  // a phone that is not there.
  return server->getConnectedCount() > 0;
}

bool take(RefCtsTime::Stamp &out) {
  if (!running) {
    return false;
  }
  const uint32_t now = millis();
  bool got = false;

  portENTER_CRITICAL(&stateLock);
  if (pendingTime &&
      (pendingOffsetKnown || (now - pendingAt) >= ZONE_GRACE_MS)) {
    out.wall          = pendingWall;
    out.haveOffset    = pendingOffsetKnown;
    out.offsetSeconds = pendingOffset;
    pendingTime       = false;
    got               = true;
  }
  portEXIT_CRITICAL(&stateLock);
  return got;
}

void end() {
  if (!running) {
    return;
  }
  running = false;
  BLEDevice::stopAdvertising();
  // deinit(false), not deinit(true). The `true` there releases the
  // controller's memory for good *and* leaves the library's initialized flag
  // set, so the next begin() would quietly do nothing and the second sync of
  // a session would sit advertising to nobody. `false` shuts the controller
  // down just as thoroughly and lets it come back up.
  BLEDevice::deinit(false);
  server        = nullptr;
  currentTime   = nullptr;
  localTimeInfo = nullptr;
  clearPending();
}

} // namespace RefBleTime

#else // REF_BLE_AVAILABLE

// No Bluetooth in this build. Everything reports "not supported" and the menu
// leaves the row out; nothing else in the sketch needs to know.
namespace RefBleTime {

bool supported() { return false; }
bool begin() { return false; }
void publish(const struct tm &, long) {}
bool connected() { return false; }
bool take(RefCtsTime::Stamp &) { return false; }
void end() {}
const char *deviceName() { return BT_DEVICE_NAME; }

} // namespace RefBleTime

#endif // REF_BLE_AVAILABLE
