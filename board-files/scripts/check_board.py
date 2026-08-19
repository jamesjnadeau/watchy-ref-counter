#!/usr/bin/env python3
"""Check the PCB against the compiled netlist, headless.

Reads the board back off disk and asserts, independently of the script that
wrote it:

  * every component in the netlist has a footprint on the board, with the
    footprint the netlist asks for
  * the board has no footprint the netlist does not name, except the
    graphical ones (display outline, logos)
  * every pad the netlist assigns is on the right net
  * no pad is on a net the netlist does not mention
  * the mechanical parts are still at V2's coordinates
  * the board is 4 copper layers with a GND and a +3V3 plane
  * the module's antenna keepout hangs off the board edge and forbids pour

It does not check routing, and there is none. Run it after
scripts/apply_netlist.py.

    /usr/bin/python3.12 scripts/check_board.py
"""

import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "checks"))
from netlist_check import load  # noqa: E402

BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-s3.kicad_pcb")
NETLIST = os.path.join(ROOT, "build/default.net")

GRAPHICAL_PREFIXES = ("REF", "G")

# The frozen mechanical envelope: V2's coordinates, in mm, for the parts a
# case has to line up with. Measured off upstream's Watchy.kicad_pcb.
MECHANICAL = {
    "SW1": (100.570, 106.330),
    "SW2": (100.570, 79.930),
    "SW3": (70.630, 79.930),
    "SW4": (70.630, 106.330),
    "J1": (85.475, 99.770),
    "J3": (96.830, 84.720),
}
MECHANICAL_TOLERANCE_MM = 0.001

PLANES = {"In1.Cu": "GND", "In2.Cu": "+3V3"}


def is_graphical(ref):
    return any(ref.startswith(p) for p in GRAPHICAL_PREFIXES)


def check(board, comps, nets):
    problems = {}

    def fail(group, msg):
        problems.setdefault(group, []).append(msg)

    fps = {}
    for fp in board.GetFootprints():
        fps.setdefault(fp.GetReference(), []).append(fp)

    # ------------------------------------------------------------ components
    for ref in sorted(comps):
        got = fps.get(ref)
        if not got:
            fail("components", "%s is in the netlist but not on the board"
                 % ref)
            continue
        if len(got) > 1:
            fail("components", "%s appears %d times on the board"
                 % (ref, len(got)))
        want = comps[ref]["footprint"]
        have = got[0].GetFPIDAsString()
        if have not in (want, want.split(":")[-1]):
            fail("components", "%s is %s on the board, netlist says %s"
                 % (ref, have, want))
    for ref in sorted(fps):
        if ref not in comps and not is_graphical(ref):
            fail("components", "%s is on the board but not in the netlist"
                 % ref)

    # ------------------------------------------------------------------ nets
    wanted = {}
    for name, nodes in nets.items():
        for ref, pad in nodes:
            wanted[(ref, pad)] = name

    for (ref, padname), netname in sorted(wanted.items()):
        got = fps.get(ref)
        if not got:
            continue
        pads = [p for p in got[0].Pads() if p.GetPadName() == padname]
        if not pads:
            fail("nets", "%s has no pad %s" % (ref, padname))
            continue
        for p in pads:
            if p.GetNetname() != netname:
                fail("nets", "%s.%s is on %r, netlist says %r"
                     % (ref, padname, p.GetNetname(), netname))

    for ref, got in sorted(fps.items()):
        if is_graphical(ref):
            continue
        for p in got[0].Pads():
            key = (ref, p.GetPadName())
            if key not in wanted and p.GetNetname():
                fail("nets", "%s.%s is on %r but the netlist leaves it "
                     "unconnected" % (ref, p.GetPadName(), p.GetNetname()))

    # ------------------------------------------------------------ mechanical
    for ref, (x, y) in sorted(MECHANICAL.items()):
        got = fps.get(ref)
        if not got:
            fail("mechanical", "%s is missing" % ref)
            continue
        pos = got[0].GetPosition()
        dx = abs(pcbnew.ToMM(pos.x) - x)
        dy = abs(pcbnew.ToMM(pos.y) - y)
        if dx > MECHANICAL_TOLERANCE_MM or dy > MECHANICAL_TOLERANCE_MM:
            fail("mechanical", "%s moved to (%.3f, %.3f); V2 has it at "
                 "(%.3f, %.3f), and a case is cut to V2"
                 % (ref, pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y), x, y))

    # ---------------------------------------------------------------- stackup
    if board.GetCopperLayerCount() != 4:
        fail("stackup", "board has %d copper layers, spec D9 says 4"
             % board.GetCopperLayerCount())
    found = {}
    for zone in board.Zones():
        if zone.GetIsRuleArea():
            continue
        found.setdefault(board.GetLayerName(zone.GetLayer()),
                         []).append(zone.GetNetname())
    for layer, netname in sorted(PLANES.items()):
        if netname not in found.get(layer, []):
            fail("stackup", "%s has no %s plane (found %s)"
                 % (layer, netname, found.get(layer) or "nothing"))

    # ----------------------------------------------------------------- antenna
    module = fps.get("U1", [None])[0]
    if module is None:
        fail("antenna", "U1 is missing")
    else:
        edges = board.GetBoardEdgesBoundingBox()
        keepouts = [z for z in module.Zones() if z.GetIsRuleArea()]
        if not keepouts:
            fail("antenna", "U1's footprint carries no keepout zone; the "
                 "module antenna needs one on every copper layer")
        for z in keepouts:
            if z.GetDoNotAllowCopperPour() is False:
                fail("antenna", "U1's keepout allows copper pour")
            box = z.Outline().BBox()
            if box.GetTop() >= edges.GetTop():
                fail("antenna", "U1's antenna keepout starts at y=%.2f, "
                     "inside the board edge at y=%.2f; the antenna end must "
                     "overhang" % (pcbnew.ToMM(box.GetTop()),
                                   pcbnew.ToMM(edges.GetTop())))
    return problems


def main():
    comps, nets = load(NETLIST)
    board = pcbnew.LoadBoard(BOARD)
    problems = check(board, comps, nets)

    groups = ["components", "nets", "mechanical", "stackup", "antenna"]
    total = 0
    for g in groups:
        items = problems.get(g, [])
        total += len(items)
        if items:
            print("FAIL %s" % g)
            for i in items[:20]:
                print("       %s" % i)
            if len(items) > 20:
                print("       ... and %d more" % (len(items) - 20))
        else:
            print("ok   %s" % g)
    print()
    print("%d problem(s)" % total)
    print("routing is NOT checked, and the board is NOT routed")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
