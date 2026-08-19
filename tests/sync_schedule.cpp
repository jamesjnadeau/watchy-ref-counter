// Host test for the due-time arithmetic in RefSyncSchedule.cpp. Pure
// arithmetic: no Arduino headers, no radio, so it runs anywhere.
#include "RefSyncSchedule.h"
#include <cstdio>

static int failures = 0;

static const time_t NOW = 1750000000;

static void expectEq(const char *what, uint32_t got, uint32_t want) {
  if (got != want) {
    printf("FAIL %-56s want %u got %u\n", what, want, got);
    failures++;
  } else {
    printf("ok   %-56s %u\n", what, got);
  }
}

int main() {
  using RefSyncSchedule::secondsUntilDue;
  using RefSyncSchedule::NEVER;

  // Quiet period off beats everything else, even a floor that would
  // otherwise be satisfied.
  expectEq("quiet off: never, even though floor is due",
           secondsUntilDue(NOW, NOW - 100 * 3600, NOW - 100 * 3600, 0, 24),
           NEVER);

  // RTC never set: nothing to schedule against.
  expectEq("clock never set: never",
           secondsUntilDue(0, 0, 0, 3, 24), NEVER);

  // Pressed 2h ago, never synced: quiet period has 1h left to run.
  expectEq("pressed 2h ago, never synced: 1h left",
           secondsUntilDue(NOW, NOW - 2 * 3600, 0, 3, 24), 3600);

  // Never pressed and never synced, on a clock that has since been set: this
  // is what a cold boot leaves behind, and it needs no special case. Both
  // anchors sit at the epoch, so both windows closed decades ago.
  expectEq("both anchors zero, clock set: due now",
           secondsUntilDue(NOW, 0, 0, 3, 24), 0);

  // Pressed 3h ago exactly: boundary is inclusive.
  expectEq("pressed 3h ago exactly: due now",
           secondsUntilDue(NOW, NOW - 3 * 3600, 0, 3, 24), 0);

  // Pressed 4h ago (quiet period long satisfied), synced 1h ago: the 24h
  // floor since the last sync attempt dominates.
  expectEq("pressed 4h ago, synced 1h ago: floor dominates",
           secondsUntilDue(NOW, NOW - 4 * 3600, NOW - 1 * 3600, 3, 24),
           23 * 3600);

  // Pressed 4h ago, synced 25h ago: both windows have already elapsed.
  expectEq("pressed 4h ago, synced 25h ago: both satisfied",
           secondsUntilDue(NOW, NOW - 4 * 3600, NOW - 25 * 3600, 3, 24), 0);

  // Pressed 10 min ago, synced 30h ago: the floor is long satisfied, but the
  // quiet period since the recent press still has time left.
  expectEq("pressed 10min ago, synced 30h ago: quiet dominates",
           secondsUntilDue(NOW, NOW - 10 * 60, NOW - 30 * 3600, 3, 24),
           2 * 3600 + 50 * 60);

  // Floor off, pressed 3h ago (quiet satisfied), synced 1 min ago: nothing
  // left to wait for.
  expectEq("floor off: due once quiet period passes",
           secondsUntilDue(NOW, NOW - 3 * 3600, NOW - 60, 3, 0), 0);

  // Clock set backwards relative to the last activity: treat as due rather
  // than wait out a bogus interval.
  expectEq("lastActivity in the future: due now",
           secondsUntilDue(NOW, NOW + 3600, 0, 3, 24), 0);

  // Clock set backwards relative to the last sync attempt: same rule.
  expectEq("lastSyncAttempt in the future: due now",
           secondsUntilDue(NOW, 0, NOW + 3600, 3, 24), 0);

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures != 0;
}
