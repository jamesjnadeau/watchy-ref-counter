#include "RefSyncSchedule.h"

namespace RefSyncSchedule {

uint32_t secondsUntilDue(time_t now, time_t lastActivity, time_t lastSyncAttempt,
                          uint32_t quietHours, uint32_t minIntervalHours) {
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
