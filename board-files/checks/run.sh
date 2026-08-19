#!/usr/bin/env bash
# Design checks for the S3-MINI-1 / RV-3028-C7 board.
#
# Compiles the atopile sources to a KiCad netlist, then asserts the
# constraints the spec cares about -- the pin map, the RTC-capable wake pins,
# ADC1 for the battery tap, the USB-C pulldowns, the RTC supply rails, the
# blocks carried over from watchy-hardware v2.0, and the parts this board
# deletes. Finally it writes the fab BOM.
#
# This does NOT run ERC or DRC. KiCad 8+ is needed for `kicad-cli sch erc`
# and `kicad-cli pcb drc`, and the newest version installable in this
# environment is 7.0.11, which has neither. See UNVERIFIED.md.
#
#   ./checks/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."

ATO="${ATO:-ato}"
if ! command -v "$ATO" >/dev/null 2>&1; then
  echo "ato not on PATH. Install it with:" >&2
  echo "    python3 -m venv ~/.venvs/ato && ~/.venvs/ato/bin/pip install atopile" >&2
  echo "then re-run as: ATO=~/.venvs/ato/bin/ato ./checks/run.sh" >&2
  exit 127
fi

echo "=== build"
"$ATO" --non-interactive build

echo
echo "=== assertions"
python3 checks/netlist_check.py build/default.net

echo
echo "=== bom"
mkdir -p fab
python3 checks/make_bom.py build/default.net fab/BOM.csv

echo
echo "=== not checked here"
sed -n '/^- /p' UNVERIFIED.md
