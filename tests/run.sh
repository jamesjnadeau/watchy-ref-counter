#!/usr/bin/env bash
# Host tests for the parts of the sketch that are pure arithmetic.
#
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp
# and RefMenuItems.cpp are the files with logic worth testing off the watch,
# and board.h's pin maps are checked here too. Between them: no
# panel, no GPIO, and the sort of bugs -- a daylight saving date, a digit
# running off the edge of the screen, a clamp, a due-time boundary, a menu row
# in the wrong place -- that are expensive to find on hardware. They compile
# here against a stub Preferences; everything else is the shipped source.
#
#   ./tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT

declare -A SOURCES=(
  [tz_test]="RefCounter/RefZone.cpp"
  [tz_edges]="RefCounter/RefZone.cpp"
  [segments_test]="RefCounter/RefSegments.cpp"
  [sport_test]="RefCounter/RefSport.cpp"
  [sync_schedule]="RefCounter/RefSyncSchedule.cpp"
  [wifi_test]="RefCounter/RefWifi.cpp"
  [cts_time_test]="RefCounter/RefCtsTime.cpp"
  [menu_items_test]="RefCounter/RefMenuItems.cpp RefCounter/RefSport.cpp RefCounter/RefZone.cpp"
  [board_test]=""
  [board_c6_test]=""
)

for t in tz_test tz_edges segments_test sport_test sync_schedule wifi_test menu_items_test board_test board_c6_test; do
  $CXX -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter \
       -o "$out/$t" "tests/$t.cpp" ${SOURCES[$t]}
  echo "=== $t"
  "$out/$t"
  echo
done

# board.h has to refuse the retired revision flags outright. A leftover -D, or
# an Arduino IDE user's stale #define, must stop the build rather than quietly
# selecting the V2.0 map -- on V1 hardware the battery tap and the up button
# are on other pins entirely. This is a compile that has to fail, which is why
# it lives here rather than in a .cpp.
echo "=== board_guard"
for flag in ARDUINO_WATCHY_V10 ARDUINO_WATCHY_V15 ARDUINO_ESP32S3_DEV; do
  if err=$($CXX -std=c++17 -Itests/stub -IRefCounter -fsyntax-only \
                -D "$flag" -xc++ - <<<'#include "board.h"' 2>&1); then
    echo "FAIL: board.h accepted -D $flag" >&2
    exit 1
  fi
  case "$err" in
    *"no longer supported"*) echo "  -D $flag rejected" ;;
    *) echo "FAIL: -D $flag failed, but not on the guard:" >&2
       echo "$err" >&2
       exit 1 ;;
  esac
done
echo
