#include "RefZone.h"

#include <Preferences.h>
#include <string.h>

#include "settings.h"

namespace RefZone {
namespace {

struct ZoneInfo {
  const char *name;    // menu label, <= 8 chars so the picker rows fit
  const char *stdAbbr;
  const char *dstAbbr; // nullptr when the zone never shifts
  int16_t     stdOffsetMinutes;
  const char *region;  // <= 17 chars, shown under the picker
};

// East to west, then the territories. Offsets are standard time; the daylight
// shift is always exactly one hour in the US.
const ZoneInfo ZONES[] = {
    {"Eastern",  "EST",  "EDT",   -300, "NY DC ATL MIA"},
    {"Central",  "CST",  "CDT",   -360, "CHI DAL MSP NOLA"},
    {"Mountain", "MST",  "MDT",   -420, "DEN SLC ABQ"},
    {"Arizona",  "MST",  nullptr, -420, "most of Arizona"},
    {"Pacific",  "PST",  "PDT",   -480, "LA SEA SFO LAS"},
    {"Alaska",   "AKST", "AKDT",  -540, "most of Alaska"},
    {"Aleutian", "HST",  "HDT",   -600, "west Aleutians"},
    {"Hawaii",   "HST",  nullptr, -600, "Hawaii"},
    {"Samoa",    "SST",  nullptr, -660, "American Samoa"},
    {"Atlantic", "AST",  nullptr, -240, "Puerto Rico USVI"},
    {"Chamorro", "ChST", nullptr,  600, "Guam N Marianas"},
};
const uint8_t ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);

// NVS. Both settings survive a reflash, which is the point -- the zone is a
// property of where the watch is, not of the firmware on it.
const char *NVS_NAMESPACE = "refzone";
const char *NVS_KEY_ZONE  = "tz";
const char *NVS_KEY_DST   = "dst";

uint8_t selected = 0;
bool    autoDst  = true;

uint8_t clampIndex(uint8_t i) { return i < ZONE_COUNT ? i : 0; }

// --- Calendar arithmetic ---------------------------------------------------
// Days since 1970-01-01 for a civil date, by Howard Hinnant's algorithm (the
// public domain chrono-compatible date routines). Done by hand rather than
// through mktime so the daylight saving rule below cannot be thrown off by
// whatever the TZ environment variable happens to be set to at the time.
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= (m <= 2) ? 1 : 0;
  const int32_t  era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);              // 0..399
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // 0..146096
  return era * 146097 + (int32_t)doe - 719468;
}

// Day of the month of the first Sunday. 1970-01-01 was a Thursday, so adding
// four lines the day count up with Sunday at zero.
uint32_t firstSunday(int32_t year, uint32_t month) {
  const int32_t first = daysFromCivil(year, month, 1);
  const int32_t wday  = (first + 4) % 7;
  return 1u + (uint32_t)((7 - wday) % 7);
}

// Whether US daylight saving is in force, given a local *standard* time
// expressed as seconds since the epoch.
bool usDstActive(time_t localStandard) {
  struct tm t;
  // gmtime_r, not localtime_r: the value is already shifted into local
  // standard time, so no further adjustment must be applied to it.
  gmtime_r(&localStandard, &t);
  const int32_t year = t.tm_year + 1900;

  // Forward at 02:00 standard on the second Sunday in March.
  const time_t start =
      (time_t)daysFromCivil(year, 3, firstSunday(year, 3) + 7) * 86400 + 2 * 3600;
  // Back at 02:00 daylight on the first Sunday in November, which is 01:00
  // standard -- the comparison is being made in standard time.
  const time_t end =
      (time_t)daysFromCivil(year, 11, firstSunday(year, 11)) * 86400 + 1 * 3600;

  return localStandard >= start && localStandard < end;
}

} // namespace

uint8_t count() { return ZONE_COUNT; }
const char *name(uint8_t i) { return ZONES[clampIndex(i)].name; }
const char *region(uint8_t i) { return ZONES[clampIndex(i)].region; }
int16_t standardOffsetMinutes(uint8_t i) {
  return ZONES[clampIndex(i)].stdOffsetMinutes;
}
const char *standardAbbrev(uint8_t i) { return ZONES[clampIndex(i)].stdAbbr; }
const char *daylightAbbrev(uint8_t i) { return ZONES[clampIndex(i)].dstAbbr; }
bool observesDst(uint8_t i) { return ZONES[clampIndex(i)].dstAbbr != nullptr; }

void begin() {
  // Resolve the compiled-in default first, so a watch with nothing stored yet
  // still shows a sensible time.
  selected = 0;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (strcmp(ZONES[i].name, DEFAULT_TIME_ZONE) == 0) {
      selected = i;
      break;
    }
  }
  autoDst = DEFAULT_DST_AUTO;

  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) { // read only
    selected = clampIndex(prefs.getUChar(NVS_KEY_ZONE, selected));
    autoDst  = prefs.getBool(NVS_KEY_DST, autoDst);
    prefs.end();
  }
}

uint8_t index() { return selected; }

void setIndex(uint8_t i) {
  selected = clampIndex(i);
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUChar(NVS_KEY_ZONE, selected);
    prefs.end();
  }
}

bool dstAuto() { return autoDst; }

void setDstAuto(bool on) {
  autoDst = on;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_DST, autoDst);
    prefs.end();
  }
}

bool dstActiveAt(time_t utc) {
  if (!autoDst || !observesDst(selected)) {
    return false;
  }
  const long stdOffset = (long)ZONES[selected].stdOffsetMinutes * 60L;
  return usDstActive(utc + stdOffset);
}

long offsetSecondsAt(time_t utc) {
  long offset = (long)ZONES[selected].stdOffsetMinutes * 60L;
  if (dstActiveAt(utc)) {
    offset += 3600L;
  }
  return offset;
}

long offsetSecondsForLocal(time_t local) {
  long offset = (long)ZONES[selected].stdOffsetMinutes * 60L;
  if (autoDst && observesDst(selected) && usDstActive(local)) {
    offset += 3600L;
  }
  return offset;
}

const char *abbrevAt(time_t utc) {
  return dstActiveAt(utc) ? ZONES[selected].dstAbbr : ZONES[selected].stdAbbr;
}

} // namespace RefZone
