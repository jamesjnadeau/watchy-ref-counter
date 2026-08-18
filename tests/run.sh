#!/usr/bin/env bash
# Host tests for the time zone and daylight saving rule.
#
# RefZone.cpp is the only file in the sketch with logic worth testing off the
# watch: it is pure arithmetic, and getting a changeover date wrong is the sort
# of bug that only shows up twice a year. It compiles here against a stub
# Preferences (which reports nothing stored, so the settings.h defaults apply);
# everything else in the file is the shipped source.
#
#   ./tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT

for t in tz_test tz_edges; do
  $CXX -std=c++17 -Wall -Wextra -Itests/stub -IRefCounter \
       -o "$out/$t" "tests/$t.cpp" RefCounter/RefZone.cpp
  echo "=== $t"
  "$out/$t"
  echo
done
