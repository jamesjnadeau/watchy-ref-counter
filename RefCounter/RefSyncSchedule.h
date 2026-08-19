#ifndef REF_SYNC_SCHEDULE_H
#define REF_SYNC_SCHEDULE_H

#include <stdint.h>
#include <time.h>

// When the next automatic NTP resync falls due.
//
// RefClock.cpp pulls in WiFi.h, Wire.h and esp_sntp.h to actually talk to
// the radio and the RTC, so it cannot be compiled on a host. The due-time
// arithmetic is the only part worth a unit test, so it lives here instead,
// with no Arduino dependency, alongside RefZone and RefSegments.

namespace RefSyncSchedule {

// Returned when no automatic sync can be scheduled at all.
static const uint32_t NEVER = 0xFFFFFFFFUL;

// Seconds until the next automatic sync falls due, given the current UTC
// epoch, the epoch of the last button press or sleep entry, and the epoch of
// the last sync attempt. 0 means due now; NEVER means nothing to schedule.
uint32_t secondsUntilDue(time_t now, time_t lastActivity, time_t lastSyncAttempt,
                          uint32_t quietHours, uint32_t minIntervalHours);

} // namespace RefSyncSchedule

#endif // REF_SYNC_SCHEDULE_H
