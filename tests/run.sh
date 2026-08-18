#!/usr/bin/env bash
# Host tests for the parts of the sketch that are pure arithmetic.
#
# RefZone.cpp, RefSegments.cpp and RefSport.cpp are the only files with logic
# worth testing off the watch: no panel, no GPIO, and the sort of bugs -- a
# daylight saving date, a digit running off the edge of the screen, a clamp --
# that are expensive to find on hardware. They compile here against a stub
# Preferences; everything else is the shipped source.
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
)

for t in tz_test tz_edges segments_test; do
  $CXX -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter \
       -o "$out/$t" "tests/$t.cpp" ${SOURCES[$t]}
  echo "=== $t"
  "$out/$t"
  echo
done
