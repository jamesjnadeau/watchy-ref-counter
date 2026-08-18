// Host test for the daylight saving rule in RefZone.cpp. Compiles the real
// source against a stub Preferences, so what is under test is the shipped code.
#include "RefZone.h"
#include <cstdio>
#include <cstring>
#include <ctime>

static int failures = 0;

static void pick(const char *name) {
  for (uint8_t i = 0; i < RefZone::count(); i++)
    if (!strcmp(RefZone::name(i), name)) { RefZone::setIndex(i); return; }
  printf("NO SUCH ZONE %s\n", name); failures++;
}

static time_t utc(int y, int mo, int d, int h, int mi) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi;
  return timegm(&t);
}

static void expectOffset(const char *what, time_t at, long wantHours) {
  const long got = RefZone::offsetSecondsAt(at);
  if (got != wantHours * 3600) {
    printf("FAIL %-46s want UTC%+ld got UTC%+ld\n", what, wantHours, got / 3600);
    failures++;
  } else {
    printf("ok   %-46s UTC%+ld %s\n", what, wantHours, RefZone::abbrevAt(at));
  }
}

int main() {
  RefZone::begin();
  printf("default zone: %s, dstAuto=%d\n\n", RefZone::name(RefZone::index()),
         RefZone::dstAuto());

  // --- Eastern: exact transition instants, 2024 through 2027 ---------------
  // US rule: forward 02:00 std second Sunday in March (07:00 UTC for EST),
  // back 02:00 dst first Sunday in November (06:00 UTC for EDT).
  pick("Eastern");
  struct { int y, mar, nov; } yrs[] = {
      {2024, 10, 3}, {2025, 9, 2}, {2026, 8, 1}, {2027, 14, 7}, {2030, 10, 3},
  };
  for (auto &k : yrs) {
    char b[80];
    snprintf(b, sizeof b, "%d-03-%02d 06:59 UTC (one min before spring)", k.y, k.mar);
    expectOffset(b, utc(k.y, 3, k.mar, 6, 59), -5);
    snprintf(b, sizeof b, "%d-03-%02d 07:00 UTC (spring forward)", k.y, k.mar);
    expectOffset(b, utc(k.y, 3, k.mar, 7, 0), -4);
    snprintf(b, sizeof b, "%d-11-%02d 05:59 UTC (one min before fall back)", k.y, k.nov);
    expectOffset(b, utc(k.y, 11, k.nov, 5, 59), -4);
    snprintf(b, sizeof b, "%d-11-%02d 06:00 UTC (fall back)", k.y, k.nov);
    expectOffset(b, utc(k.y, 11, k.nov, 6, 0), -5);
  }
  printf("\n");

  // --- Deep winter and high summer across every zone ----------------------
  const time_t jan = utc(2026, 1, 15, 18, 0);
  const time_t jul = utc(2026, 7, 15, 18, 0);
  struct { const char *z; long win, sum; } zones[] = {
      {"Eastern", -5, -4},  {"Central", -6, -5},  {"Mountain", -7, -6},
      {"Arizona", -7, -7},  {"Pacific", -8, -7},  {"Alaska", -9, -8},
      {"Aleutian", -10, -9},{"Hawaii", -10, -10}, {"Samoa", -11, -11},
      {"Atlantic", -4, -4}, {"Chamorro", 10, 10},
  };
  for (auto &z : zones) {
    pick(z.z);
    char b[80];
    snprintf(b, sizeof b, "%-8s January", z.z); expectOffset(b, jan, z.win);
    snprintf(b, sizeof b, "%-8s July", z.z);    expectOffset(b, jul, z.sum);
  }
  printf("\n");

  // --- The switch turned off pins every zone to standard time -------------
  RefZone::setDstAuto(false);
  pick("Eastern");
  expectOffset("Eastern July, DST switch off", jul, -5);
  pick("Pacific");
  expectOffset("Pacific July, DST switch off", jul, -8);
  RefZone::setDstAuto(true);
  printf("\n");

  // --- setLocal/localNow round trip, away from the transition windows -----
  pick("Pacific");
  int roundTripFails = 0;
  for (int day = 1; day <= 365; day += 1) {
    for (int hour = 0; hour < 24; hour += 6) {
      const time_t localWanted = utc(2026, 1, 1, hour, 30) + (time_t)(day - 1) * 86400;
      // Mirror RefClock: local -> UTC -> local.
      const time_t asUtc = localWanted - RefZone::offsetSecondsForLocal(localWanted);
      const time_t back  = asUtc + RefZone::offsetSecondsAt(asUtc);
      if (back != localWanted) {
        if (roundTripFails < 6) {
          struct tm t; gmtime_r(&localWanted, &t);
          printf("     round trip off by %+lds at %04d-%02d-%02d %02d:%02d local\n",
                 (long)(back - localWanted), t.tm_year + 1900, t.tm_mon + 1,
                 t.tm_mday, t.tm_hour, t.tm_min);
        }
        roundTripFails++;
      }
    }
  }
  printf("%s round trip: %d of %d local times differ\n",
         roundTripFails <= 2 ? "ok  " : "FAIL", roundTripFails, 365 * 4);
  if (roundTripFails > 2) failures++;

  printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
  return failures != 0;
}
