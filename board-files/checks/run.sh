#!/usr/bin/env bash
# Design checks for the C6-MINI-1 / RV-3028-C7 board.
#
# Compiles the atopile sources to a KiCad netlist, then asserts the
# constraints the spec cares about -- the pin map, the RTC-capable wake pins,
# ADC1 for the battery tap, the USB-C pulldowns, the RTC supply rails, the
# blocks carried over from watchy-hardware v2.0, and the parts this board
# deletes. Finally it writes the fab BOM.
#
# It does not run ERC. There is no schematic file -- the design is atopile
# source compiled straight to a netlist -- so there is nothing for
# `kicad-cli sch erc` to read. See UNVERIFIED.md.
#
#   ./checks/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."

ATO="${ATO:-ato}"
ATO_PINNED="$(sed -n 's/^atopile==//p' requirements.txt)"
if ! command -v "$ATO" >/dev/null 2>&1; then
  echo "ato not on PATH. Install it with:" >&2
  echo "    python3 -m venv ~/.venvs/ato" >&2
  echo "    ~/.venvs/ato/bin/pip install -r requirements.txt" >&2
  echo "then re-run as: ATO=~/.venvs/ato/bin/ato ./checks/run.sh" >&2
  exit 127
fi
# The version matters. `pip install atopile` gets 0.12.6, which will not build
# these sources -- see requirements.txt for what was tried.
# 0.2.x prints "ato, version 0.2.67"; later ones print a bare version. Take
# the last whitespace-separated field either way.
ATO_VERSION="$("$ATO" --version 2>/dev/null | tr ',' ' ' | awk 'NR==1{print $NF}')"
if [ "$ATO_VERSION" != "$ATO_PINNED" ]; then
  echo "ato is $ATO_VERSION, this design is written against $ATO_PINNED." >&2
  echo "    ~/.venvs/ato/bin/pip install -r requirements.txt" >&2
  exit 1
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
echo "=== board"
# pcbnew ships with KiCad and is bound to the interpreter KiCad was built
# against, which is usually not the one on PATH.
PCBNEW_PY=""
for py in /usr/bin/python3.12 /usr/bin/python3.11 /usr/bin/python3 python3; do
  if command -v "$py" >/dev/null 2>&1 && "$py" -c "import pcbnew" 2>/dev/null; then
    PCBNEW_PY="$py"; break
  fi
done
if [ -n "$PCBNEW_PY" ]; then
  "$PCBNEW_PY" scripts/check_board.py 2>&1 | grep -vE "memory leak|Zone fills|^Warning"
else
  echo "   skipped: no interpreter with pcbnew. Install KiCad to check the layout."
fi

echo
echo "=== drc"
# Held rather than raised on the spot, so that a DRC failure still prints the
# "not checked here" list below -- that list is the most important thing in
# this output and it should not disappear the moment something goes wrong.
STATUS=0
if command -v kicad-cli >/dev/null 2>&1; then
  python3 checks/drc_check.py || STATUS=$?
else
  echo "   skipped: no kicad-cli. Install KiCad 8 or newer to check the board."
fi

echo
echo "=== not checked here"
sed -n '/^- /p' UNVERIFIED.md

exit "$STATUS"
