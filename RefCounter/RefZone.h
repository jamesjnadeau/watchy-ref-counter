#ifndef REF_ZONE_H
#define REF_ZONE_H

#include <stdint.h>
#include <time.h>

// US time zones, and the daylight saving rule that goes with them.
//
// The RTC chip holds UTC. Everything the watch shows is UTC plus whatever
// offset this module reports for that instant, which means changing zone or
// flipping the DST switch is immediate and never rewrites the clock. It also
// means a daylight saving changeover needs no action at all: the offset is a
// pure function of the moment being displayed, so the watch is right on both
// sides of the boundary and right again after a reboot in between.
//
// The rule implemented is the US federal one in force since 2007 (Energy
// Policy Act of 2005): forward at 02:00 local standard on the second Sunday in
// March, back at 02:00 local daylight on the first Sunday in November. Zones
// that do not observe it -- Arizona, Hawaii, Samoa, Puerto Rico, Guam -- are
// marked as such in the table and ignore the switch entirely.
namespace RefZone {

// Number of zones in the table.
uint8_t count();

// Menu label, e.g. "Eastern". Eight characters or fewer.
const char *name(uint8_t index);

// Where the zone applies, for the picker's footer.
const char *region(uint8_t index);

// Standard-time offset from UTC, in whole minutes. Negative west of Greenwich.
int16_t standardOffsetMinutes(uint8_t index);

// "EST", and "EDT" or nullptr when the zone never shifts.
const char *standardAbbrev(uint8_t index);
const char *daylightAbbrev(uint8_t index);
bool observesDst(uint8_t index);

// Load the saved zone and DST switch out of NVS, falling back to the defaults
// in settings.h. Call once at startup.
void begin();

uint8_t index();
void setIndex(uint8_t index); // persists immediately

// The menu's switch. On means "shift automatically when the date calls for
// it", not "shift now" -- a zone without DST is unaffected either way.
bool dstAuto();
void setDstAuto(bool on); // persists immediately

// True when daylight saving is in force at this UTC instant, for the selected
// zone and switch position.
bool dstActiveAt(time_t utc);

// Seconds to add to UTC to get local time at that instant.
long offsetSecondsAt(time_t utc);

// The reverse, for turning a time the user typed into UTC. Daylight saving has
// to be decided from the local time itself here, which is exact everywhere
// except the hour that springing forward deletes: typing 02:30 on that one
// morning stores 01:30, since 02:30 never happens. The repeated hour in
// November resolves to standard time and reads back unchanged.
long offsetSecondsForLocal(time_t local);

// "EST" or "EDT" as appropriate for that instant.
const char *abbrevAt(time_t utc);

} // namespace RefZone

#endif // REF_ZONE_H
