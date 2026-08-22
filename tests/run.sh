#!/usr/bin/env bash
# Host tests for the parts of the sketch that are pure arithmetic.
#
# RefZone.cpp, RefSegments.cpp, RefSport.cpp, RefSyncSchedule.cpp, RefWifi.cpp,
# RefCtsTime.cpp and RefMenuItems.cpp are the files with logic worth testing off
# the watch: no panel, no GPIO, no radio, and the sort of bugs -- a daylight
# saving date, a digit running off the edge of the screen, a clamp, a due-time
# boundary, a menu row in the wrong place, a Bluetooth packet believed when it
# should not be -- that are expensive to find on hardware. They compile
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
)

for t in tz_test tz_edges segments_test sport_test sync_schedule wifi_test \
         cts_time_test menu_items_test; do
  $CXX -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter \
       -o "$out/$t" "tests/$t.cpp" ${SOURCES[$t]}
  echo "=== $t"
  "$out/$t"
  echo
done
