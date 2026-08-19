#!/usr/bin/env python3
"""Run KiCad's DRC on the board and hold it to a stated policy.

`kicad-cli pcb drc` reports; it does not judge. This does the judging, and it
does it against a written-down list rather than a count, so that "DRC is
clean" cannot quietly come to mean "DRC has the same number of problems it
had last time, whatever they are now".

The policy:

  * zero unconnected items. Every pad the netlist connects must be connected
    on the board.
  * zero errors, except the ones in ACCEPTED below, each with a reason.
  * warnings are printed and counted, and the ones in ACCEPTED are not
    printed. Nothing here fails on a warning.

An accepted item is keyed by its violation type and the parts involved, not
by KiCad's exclusion hashes -- those are opaque, and they change whenever an
item moves, so they hide the thing they are meant to record.

    python3 checks/drc_check.py [board.kicad_pcb]

Needs kicad-cli 8 or newer. KiCad 7 has no `pcb drc` at all.
"""

import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-c6.kicad_pcb")

# (violation type, the parts involved) -> why this one is allowed to stand.
#
# Every entry is a decision someone made on purpose. If a violation is not
# here, it fails the build; if it is here and no longer occurs, that is
# reported too, so the list cannot rot.
ACCEPTED = {
    ("lib_footprint_issues", ("REF**",)):
        "the display outline, from V2's 'Watchy' library. Graphical only -- it "
        "is what stops anything being placed under the panel.",

    # --- the module's pads at the top edge -------------------------------
    ("copper_edge_clearance", ("U1", "segment")):
        "the module's ground pads, 0.30mm from the top edge against a 0.50mm "
        "rule. Not movable: the ESP32-S3-MINI-1 is positioned so its antenna "
        "overhangs the board edge, which is what fixes where the rest of it "
        "sits. 0.30mm is inside every fab's routed-edge capability (0.25mm is "
        "the usual figure); the rule is set conservatively for tracks, and "
        "these are pads under a module that is itself hanging off the edge. "
        "R16 and D5 were the other two and they did move -- see PLACEMENT.",

    # --- J2's own drill holes against J2's own pads -----------------------
    ("hole_clearance", ("J2",)):
        "the USB-C receptacle's two mounting holes against its own pads, "
        "0.165-0.200mm where the rule asks 0.250mm. This is inside the "
        "footprint as the manufacturer drew it -- nothing on this board put "
        "them there and no placement change can separate them. Worth one "
        "look at the datasheet's recommended land pattern before ordering.",

    # --- courtyards ------------------------------------------------------
    # The USB-C, which does not fit its slot and never will: 9.04mm of shell
    # in a 9.15mm gap, with a 10.73mm courtyard. Copper clears everywhere --
    # the shield tabs were shorting to J3 and U5 and those were fixed by
    # moving parts, see PLACEMENT -- it is only the courtyards that overlap.
    ("courtyards_overlap", ("J2", "Q4")): "USB-C against its neighbours.",
    ("courtyards_overlap", ("J2", "R14")): "USB-C against its neighbours.",
    ("courtyards_overlap", ("J2", "R16")): "USB-C against its neighbours.",
    ("courtyards_overlap", ("J2", "U5")): "USB-C against its neighbours.",
    # D5 moved 0.3mm inboard to clear the board edge and took its courtyard
    # into these two on the way. Pads still clear by 0.24mm against a 0.15mm
    # rule, so this is a courtyard overlap and nothing more.
    ("courtyards_overlap", ("D5", "R12")): "D5 moved off the board edge.",
    ("courtyards_overlap", ("D5", "R16")): "D5 moved off the board edge.",
}

# Courtyard overlaps this board inherited from watchy-hardware v2.0 with both
# parts left exactly where V2 had them. V2 is a shipped board, so these are
# overlaps that have been through a real assembly line. They are listed rather
# than waved through by rule, so that a *new* overlap between the same parts
# would still fail.
for _pair in (("C10", "C9"), ("C2", "C7"), ("C3", "D2"), ("C3", "D3"),
              ("C3", "L1"), ("C4", "L1"), ("C5", "C8"), ("C5", "C9"),
              ("C6", "C7"), ("C6", "C8"), ("Q4", "R14"), ("Q4", "R15"),
              ("R12", "R13")):
    ACCEPTED[("courtyards_overlap", _pair)] = (
        "inherited from watchy-hardware v2.0, both parts untouched.")

# Types that are warnings by policy rather than by KiCad's default severity,
# with the reason. Anything not listed keeps whatever severity KiCad gives it.
DEMOTED = {}


def refs(violation):
    """The footprint references a violation involves, as a sorted tuple."""
    out = set()
    for item in violation.get("items", []):
        text = item.get("description", "")
        match = re.search(r" of (\S+) on ", text)
        if match:
            out.add(match.group(1))
            continue
        match = re.match(r"(?:Footprint|NPTH pad of) (\S+)$", text.strip())
        if match:
            out.add(match.group(1))
            continue
        # Not attached to a footprint: a track, a via, a zone, a board edge.
        out.add(text.split(" ")[0].lower().strip("[](),"))
    return tuple(sorted(out))


def run_drc(board, report):
    cmd = ["kicad-cli", "pcb", "drc", "--severity-all", "--units", "mm",
           "--format", "json", "-o", report, board]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if not os.path.exists(report):
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit("kicad-cli pcb drc produced no report. "
                         "It needs KiCad 8 or newer.")
    return json.load(open(report, encoding="utf-8"))


def main(argv):
    board = argv[0] if argv else BOARD
    with tempfile.TemporaryDirectory() as tmp:
        result = run_drc(board, os.path.join(tmp, "drc.json"))

    violations = result.get("violations", [])
    unconnected = result.get("unconnected_items", [])
    parity = result.get("schematic_parity", [])

    errors, warnings, accepted, seen = [], [], [], set()
    for v in violations:
        key = (v["type"], refs(v))
        if key in ACCEPTED:
            seen.add(key)
            accepted.append(key)
            continue
        (errors if v["severity"] == "error" else warnings).append((key, v))

    print("== drc")
    print("   %s" % os.path.relpath(board, ROOT))
    print("   %d violation(s), %d unconnected, %d parity"
          % (len(violations), len(unconnected), len(parity)))
    print()

    if accepted:
        print("== accepted, on purpose (%d)" % len(accepted))
        for key in sorted(set(accepted)):
            n = accepted.count(key)
            print("   %-22s %-28s x%d  %s"
                  % (key[0], ",".join(key[1])[:28], n, ACCEPTED[key]))
        print()

    stale = sorted(set(ACCEPTED) - seen)
    if stale:
        print("== accepted but no longer happening (%d)" % len(stale))
        print("   Delete these from ACCEPTED -- an allowance nobody needs is")
        print("   an allowance nobody is checking.")
        for key in stale:
            print("   %-22s %s" % (key[0], ",".join(key[1])))
        print()

    by_type = {}
    for key, v in warnings:
        by_type.setdefault(key[0], []).append(key)
    if by_type:
        print("== warnings (%d)" % len(warnings))
        for t in sorted(by_type):
            parts = sorted({",".join(k[1]) for k in by_type[t]})
            print("   %-24s %3d   %s" % (t, len(by_type[t]),
                                         ", ".join(parts)[:70]))
        print()

    if errors:
        print("== ERRORS (%d)" % len(errors))
        shown = {}
        for key, v in errors:
            shown.setdefault(key, []).append(v)
        for key in sorted(shown):
            v = shown[key][0]
            print("   %-22s %-24s x%d"
                  % (key[0], ",".join(key[1])[:24], len(shown[key])))
            print("        %s" % v["description"])
        print()

    if unconnected:
        print("== UNCONNECTED (%d)" % len(unconnected))
        for u in unconnected[:20]:
            print("   %s" % u["description"])
        if len(unconnected) > 20:
            print("   ... and %d more" % (len(unconnected) - 20))
        print()

    bad = len(errors) + len(unconnected) + len(parity)
    print("%d error(s), %d unconnected, %d warning(s), %d accepted"
          % (len(errors), len(unconnected), len(warnings), len(accepted)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
