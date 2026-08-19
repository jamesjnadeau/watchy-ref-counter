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
  * the mechanical parts are still at V2's coordinates, bar two recorded moves
  * the board is 4 copper layers with a GND and a +3V3 plane
  * the design rules in the .kicad_pro are the ones apply_netlist.py sets
  * the module's antenna keepout hangs off the board edge and forbids pour
  * the outline is one closed shape with nothing cut out of the middle
  * every pad is on the board

The last two are here because both have already been wrong. V2's outline
carried two watch-strap slots, and the module and its decoupling were placed
across one of them; the USB-C receptacle was rotated 0 instead of 90 and six
of its pads hung over the right-hand edge in mid-air. Neither is visible in a
netlist and neither was caught by anything until DRC ran.

Whether the board is *routed* is not checked here -- `kicad-cli pcb drc`
does that, and checks/run.sh runs it. Run this after scripts/apply_netlist.py.

    python3 scripts/check_board.py
"""

import json
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "checks"))
from netlist_check import load  # noqa: E402

BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-c6.kicad_pcb")
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
    # J3 is 0.241mm up the board from V2: its mounting pad was sitting on one
    # of the USB-C's through-hole shield tabs, which is a short and not a
    # clearance. Every other mechanical part, the four buttons included, is
    # exactly where V2 has it. See apply_netlist.py's PLACEMENT.
    "J3": (96.830, 84.479),
}
MECHANICAL_TOLERANCE_MM = 0.001

PLANES = {"In1.Cu": "GND", "In2.Cu": "+3V3"}

# Mirrors DESIGN_RULES in apply_netlist.py, which is what writes them.
# Deliberately a second copy: this file's job is to disagree when something
# has changed the board out from under the script that generates it.
EXPECTED_MASK_EXPANSION_MM = 0.05

EXPECTED_RULES = {
    "clearance": 0.15,
    "track_width": 0.15,
    "diff_pair_width": 0.15,
    "diff_pair_gap": 0.2,
    "diff_pair_via_gap": 0.2,
    "via_diameter": 0.6,
    "via_drill": 0.3,
}


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

    # ----------------------------------------------------------- design rules
    # These live in the .kicad_pro, and loading a second board anywhere in
    # apply_netlist.py makes KiCad's settings manager flush a stale copy of
    # the project over them. That silently put the clearance back to 0.2 after
    # it had been set to 0.15, and a board routed at 0.15 came back with 76
    # clearance errors against a rule nobody had changed on purpose. Assert
    # them so it cannot happen quietly again.
    for zone in board.Zones():
        if zone.GetIsRuleArea() or zone.GetNetname() != "GND":
            continue
        if board.GetLayerName(zone.GetLayer()) not in ("F.Cu", "B.Cu"):
            continue
        if zone.GetPadConnection() != 2:      # 2 = solid
            fail("design rules", "the %s GND pour connects pads with mode %d, "
                 "apply_netlist.py sets 2 (solid)"
                 % (board.GetLayerName(zone.GetLayer()),
                    zone.GetPadConnection()))

    expansion = board.GetDesignSettings().m_SolderMaskExpansion
    if abs(pcbnew.ToMM(expansion) - EXPECTED_MASK_EXPANSION_MM) > 1e-6:
        fail("design rules", "solder mask expansion is %.3fmm, "
             "apply_netlist.py sets %.3fmm"
             % (pcbnew.ToMM(expansion), EXPECTED_MASK_EXPANSION_MM))

    project = os.path.splitext(BOARD)[0] + ".kicad_pro"
    try:
        with open(project) as fh:
            settings = json.load(fh)
    except (IOError, ValueError) as exc:
        fail("design rules", "cannot read %s: %s"
             % (os.path.basename(project), exc))
        settings = None
    if settings is not None:
        default = [c for c in settings.get("net_settings", {}).get("classes",
                                                                   [])
                   if c.get("name") == "Default"]
        if not default:
            fail("design rules", "no Default netclass in %s"
                 % os.path.basename(project))
        for cls in default:
            for key, want in sorted(EXPECTED_RULES.items()):
                got = cls.get(key)
                if got != want:
                    fail("design rules",
                         "%s is %s, apply_netlist.py sets %s" % (key, got,
                                                                 want))

    # ----------------------------------------------------------------- outline
    outline = pcbnew.SHAPE_POLY_SET()
    if not board.GetBoardPolygonOutlines(outline):
        fail("outline", "Edge.Cuts does not close into a board")
        outline = None
    else:
        if outline.OutlineCount() != 1:
            fail("outline", "Edge.Cuts makes %d separate shapes, not 1"
                 % outline.OutlineCount())
        for i in range(outline.OutlineCount()):
            for h in range(outline.HoleCount(i)):
                box = outline.Hole(i, h).BBox()
                fail("outline", "a cutout remains at x %.2f..%.2f y %.2f..%.2f"
                     % (pcbnew.ToMM(box.GetLeft()), pcbnew.ToMM(box.GetRight()),
                        pcbnew.ToMM(box.GetTop()), pcbnew.ToMM(box.GetBottom())))

    # ------------------------------------------------------------- pads on board
    if outline is not None:
        for ref, got in sorted(fps.items()):
            for pad in got[0].Pads():
                pos = pad.GetPosition()
                if not outline.Collide(pcbnew.VECTOR2I(pos.x, pos.y)):
                    fail("pads on board",
                         "%s.%s sits at (%.3f, %.3f), which is off the board"
                         % (ref, pad.GetPadName() or "?",
                            pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)))

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

    groups = ["components", "nets", "mechanical", "stackup", "design rules",
              "outline", "pads on board", "antenna"]
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
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
