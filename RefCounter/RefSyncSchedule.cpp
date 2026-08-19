// The due-time arithmetic. See RefSyncSchedule.h for why it lives apart from
// RefClock.
//
// One caveat the host test cannot catch: time_t is 64-bit on a desktop but
// 32-bit on the ESP32, so tests/sync_schedule.cpp exercises a far wider type
// than the watch ever runs. There is ample headroom at the shipped hours --
// an anchor is a present-day epoch and the additions are days at most -- but
// anyone raising them by orders of magnitude should reason about the 2038
// ceiling rather than trust a green host suite.
#include "RefSyncSchedule.h"

namespace RefSyncSchedule {

uint32_t secondsUntilDue(time_t now, time_t lastActivity,
                         time_t lastSyncAttempt, uint32_t quietHours,
                         uint32_t minIntervalHours) {
  if (quietHours == 0) {
    return NEVER; // Automatic sync is off; the floor is irrelevant.
  }
  if (now == 0) {
    return NEVER; // RTC never set: no clock to schedule against.
  }
  if (now < lastActivity || now < lastSyncAttempt) {
    return 0; // Clock set backwards: treat as due rather than wait out a
              // bogus interval.
  }

  // Do the arithmetic in time_t so a distant anchor of 0 (never pressed or
  // never synced) cannot overflow; only the final, bounded difference is
  // narrowed to uint32_t.
  const time_t quietUntil = lastActivity + (time_t)quietHours * 3600;
  const time_t floorUntil = lastSyncAttempt + (time_t)minIntervalHours * 3600;
  const time_t dueAt      = quietUntil > floorUntil ? quietUntil : floorUntil;

  return dueAt <= now ? 0 : (uint32_t)(dueAt - now);
}

} // namespace RefSyncSchedule
