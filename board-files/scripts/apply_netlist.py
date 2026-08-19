#!/usr/bin/env python3
"""Apply the compiled netlist to the PCB, headless.

KiCad's "Import Netlist" is a GUI dialog, and `kicad-cli` has no equivalent.
This does the deterministic part of that job with `pcbnew`:

  * deletes footprints the netlist no longer has (keeping graphical ones --
    the display outline and the logos have no symbol and never did)
  * replaces footprints whose part changed (U1, U3, J2)
  * adds the footprints the netlist gained, from the stock and project
    libraries
  * assigns every pad to its net, creating nets that do not exist yet
  * lays a GND pour on In1.Cu and a +3V3 pour on In2.Cu, inset from the
    board edge. They are left unfilled -- KiCad fills them, and the module's
    own keepout zone (copperpour not_allowed on *.Cu) carves the antenna
    clearance out of both at fill time
  * nudges the small parts it placed off their neighbours, and reports every
    courtyard overlap it could not resolve, with how deep it is

It does NOT route anything, and it does not pretend the result is a finished
layout. Placement of the new parts is a real design decision -- see the
"conflicts" report it prints, and README.md.

    ./scripts/apply_netlist.py            # writes the board in place
    ./scripts/apply_netlist.py --dry-run  # report only

Needs KiCad's python bindings: on Debian/Ubuntu that is the `kicad` package,
and the interpreter must be the system one that ships them (python3.12 here),
not a venv.
"""

import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "checks"))
from netlist_check import load  # noqa: E402

BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-s3.kicad_pcb")
# V2's layout, converted to the modern format and given a 4-layer stackup, but
# otherwise untouched. It is the reference the overlap report diffs against.
# Reading the baseline out of BOARD instead would be self-excusing: after the
# first run BOARD is this script's own output, so every overlap it introduced
# would come back labelled "V2 already had this".
BASELINE = os.path.join(ROOT, "elec/layout/v2-baseline.kicad_pcb")
NETLIST = os.path.join(ROOT, "build/default.net")
PROJECT_LIB = os.path.join(ROOT, "elec/footprints/footprints.pretty")
STOCK_LIB = "/usr/share/kicad/footprints"

# Footprints with no symbol: the display outline and the silkscreen logos.
# They are not in the netlist and never were, and deleting them would take the
# display keepout and the branding off the board.
GRAPHICAL_PREFIXES = ("REF", "G*")

# Placement for the parts the netlist gained, in board coordinates.
#
# The module is the one position that is forced rather than chosen. Its
# antenna keepout runs from y=-7.7 to y=-12.8 in footprint coordinates, so
# putting the module origin 7.7mm below the board's top edge puts the whole
# keepout off the board, which is the only way an on-board module antenna
# works. x is the board centreline.
#
# Everything else is a first guess near the part it serves. Anything that
# collides is reported, not silently overlapped.
EDGE_TOP = 70.15      # board outline, centreline
BOARD_CENTRE_X = 85.6

PLACEMENT = {
    "U1": (BOARD_CENTRE_X, EDGE_TOP + 7.7, 0),
    # RTC below the module, on the I2C side. V2's U6 spot is under the module
    # now.
    "U6": (80.5, 89.6, 0),
    "C15": (78.0, 89.6, 0),
    # USB-C keeps V2's micro-USB position so the case cutout moves as little
    # as possible.
    "J2": (100.08, 93.68, 0),
    "U3": (95.0, 91.7, 0),
    "R21": (97.5, 90.0, 90),
    "R22": (98.6, 90.0, 90),
    "R23": (87.5, 88.0, 0),
    "C16": (99.5, 90.6, 0),
    "TP7": (88.5, 105.8, 0),
    "TP8": (88.5, 108.4, 0),
    "TP9": (82.0, 92.2, 0),
}

# Parts the new layout displaces, and the pad each one should sit next to.
#
# The module is 15.4 x 20.5mm where V2 had a 7 x 7mm QFN, and the USB-C is
# bigger than the micro-USB it replaces, so seven of V2's parts are now under
# something. Rather than shuffling them somewhere arbitrary, each is anchored
# to the pad it actually serves and placed at the nearest clear spot to it,
# which is what you would do by hand and is what decoupling wants anyway.
#
# None of these is a mechanical part -- the switches, the FPC and the battery
# connector are frozen at V2's coordinates and check_board.py asserts it.
RELOCATE = [
    ("C12", ("U1", "3"), "+3V3 bulk decoupling, hard against the module's 3V3 pad"),
    ("C1", ("U1", "45"), "EN capacitor, beside the EN pad"),
    ("R1", ("U1", "45"), "EN pull-up, beside the EN pad"),
    ("R7", ("U6", "2"), "RTC_INT pull-up, beside the RTC's INT pin"),
    ("Q5", ("M1", "2"), "motor driver, beside the motor pads"),
    ("U2", ("Q4", "2"), "LDO, on its own input node"),
    ("D4", ("Q4", "2"), "OR-ing diode, on the same node"),
]
RELOCATE_RADIUS_MM = 14.0
# Past this, "nearest clear spot" stops being a good answer and wants a human.
FAR_FROM_ANCHOR_MM = 5.0

# How far from its preferred spot a small part may be nudged to find clear
# space, and the step used while searching. Big parts (the module, the
# connectors) are never nudged: where they go is a design decision.
NUDGE_RADIUS_MM = 6.0
NUDGE_STEP_MM = 0.5
NUDGE_EXEMPT = {"U1", "J2", "J1", "J3", "M1", "SW1", "SW2", "SW3", "SW4"}

# Parking column for anything that could not be placed: off the right edge,
# clear of the board, in a neat stack so it is obvious they need attention.
PARK_X = 112.0
PARK_Y0 = 72.0
PARK_PITCH = 3.5

PLANES = [(pcbnew.In1_Cu, "GND"), (pcbnew.In2_Cu, "+3V3")]
PLANE_INSET_MM = 0.3


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def is_graphical(ref):
    return any(ref.startswith(p[:-1] if p.endswith("*") else p)
               for p in GRAPHICAL_PREFIXES)


def lib_and_name(footprint_id):
    """'Resistor_SMD:R_0402_1005Metric' -> (library path, footprint name)."""
    if ":" not in footprint_id:
        raise ValueError("footprint %r has no library prefix" % footprint_id)
    lib, name = footprint_id.split(":", 1)
    if lib == "footprints":
        return PROJECT_LIB, name
    return os.path.join(STOCK_LIB, lib + ".pretty"), name


def get_net(board, name, cache):
    if name in cache:
        return cache[name]
    net = board.FindNet(name)
    if net is None:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
    cache[name] = net
    return net


def courtyard_box(fp):
    """Bounding box of the courtyard, falling back to the whole footprint.

    The cache is rebuilt first: a footprint loaded from a library carries a
    courtyard computed at the library origin, and SetPosition does not
    invalidate it. Skipping this reports every freshly placed part as
    overlapping every other one, near (0, 0).
    """
    fp.BuildCourtyardCaches()
    box = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
    if box.GetWidth() == 0 or box.GetHeight() == 0:
        box = fp.GetBoundingBox(False, False)
    return box


# Courtyards that merely touch are normal and fine -- V2 abuts several. Only
# a real interpenetration is worth reporting, so both axes must overlap by
# more than this before it counts.
OVERLAP_TOLERANCE_MM = 0.05


def overlaps(a, b):
    tol = mm(OVERLAP_TOLERANCE_MM)
    dx = min(a.GetRight(), b.GetRight()) - max(a.GetLeft(), b.GetLeft())
    dy = min(a.GetBottom(), b.GetBottom()) - max(a.GetTop(), b.GetTop())
    return dx > tol and dy > tol


def overlap_pairs(footprints):
    """{ref, ref} pair -> how deep the courtyards interpenetrate, in mm."""
    items = [(fp.GetReference(), courtyard_box(fp)) for fp in footprints]
    pairs = {}
    for i, (ref_a, box_a) in enumerate(items):
        for ref_b, box_b in items[i + 1:]:
            if overlaps(box_a, box_b):
                dx = (min(box_a.GetRight(), box_b.GetRight())
                      - max(box_a.GetLeft(), box_b.GetLeft()))
                dy = (min(box_a.GetBottom(), box_b.GetBottom())
                      - max(box_a.GetTop(), box_b.GetTop()))
                pairs[frozenset((ref_a, ref_b))] = min(pcbnew.ToMM(dx),
                                                       pcbnew.ToMM(dy))
    return pairs


def pad_position(fp, padname):
    for p in fp.Pads():
        if p.GetPadName() == padname:
            pos = p.GetPosition()
            return pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
    return None


def free_spot(fp, preferred, occupied, edges, radius=None):
    """Nearest clear position to `preferred`, searched on a spiral grid.

    Returns (x, y, moved_mm) or None if nothing within `radius` is clear. Only used for the small parts: a resistor that has to shift 1mm to
    stop overlapping its neighbour is not a design decision, but a connector
    that does not fit is.
    """
    px, py = preferred
    step = NUDGE_STEP_MM
    rings = int((radius or NUDGE_RADIUS_MM) / step) + 1
    for ring in range(rings):
        offsets = [(0, 0)] if ring == 0 else [
            (dx * step, dy * step)
            for dx in range(-ring, ring + 1)
            for dy in range(-ring, ring + 1)
            if max(abs(dx), abs(dy)) == ring
        ]
        offsets.sort(key=lambda o: o[0] ** 2 + o[1] ** 2)
        for dx, dy in offsets:
            fp.SetPosition(pt(px + dx, py + dy))
            box = courtyard_box(fp)
            if not edges.Contains(box):
                continue
            if any(overlaps(box, other) for other in occupied):
                continue
            return px + dx, py + dy, (dx ** 2 + dy ** 2) ** 0.5
    return None


def main(argv):
    dry_run = "--dry-run" in argv

    comps, nets = load(NETLIST)
    board = pcbnew.LoadBoard(BOARD)
    net_cache = {}

    # V2 packs its 1uF panel caps and its Schottky trio close enough that
    # their courtyards already overlap. Those are upstream's business, not
    # this board's, so the report below is a diff against the board as it
    # arrived, not an absolute count.
    if not os.path.exists(BASELINE):
        print("missing %s -- the overlap report needs V2's untouched layout "
              "to diff against" % BASELINE, file=sys.stderr)
        return 2
    baseline = overlap_pairs(
        fp for fp in pcbnew.LoadBoard(BASELINE).GetFootprints()
        if not is_graphical(fp.GetReference()))

    # ---------------------------------------------------------------- delete
    # pcbnew's footprint iterator does not survive a Remove(), so snapshot the
    # board once and track the surviving set ourselves from here on.
    existing = list(board.GetFootprints())
    removed, replaced, kept = [], [], []
    # Pose of every footprint that gets swapped out, so the replacement lands
    # exactly where V2 had it. Most of these swaps are only a library rename
    # (KiCad 5's Resistors_SMD is KiCad 7's Resistor_SMD), and losing V2's
    # placement over a rename would throw away the entire point of pinning
    # the designators.
    poses = {}
    for fp in existing:
        ref = fp.GetReference()
        if is_graphical(ref):
            kept.append(fp)
            continue
        if ref not in comps:
            removed.append("%s (%s)" % (ref, fp.GetValue()))
            board.Remove(fp)
            continue
        want = comps[ref]["footprint"]
        have = fp.GetFPIDAsString()
        if have not in (want, want.split(":")[-1]):
            replaced.append("%s: %s -> %s" % (ref, have, want))
            poses[ref] = (fp.GetPosition(), fp.GetOrientationDegrees(),
                          fp.IsFlipped())
            board.Remove(fp)
            continue
        kept.append(fp)

    present = {fp.GetReference() for fp in kept}

    # ------------------------------------------------------------------- add
    added, failed, nudged, added_refs = [], [], [], set()
    to_nudge = []
    parked = 0
    for ref in sorted(comps):
        if ref in present:
            continue
        fpid = comps[ref]["footprint"]
        try:
            libpath, name = lib_and_name(fpid)
            fp = pcbnew.FootprintLoad(libpath, name)
        except ValueError as exc:
            fp = None
            failed.append("%s: %s" % (ref, exc))
            continue
        if fp is None:
            failed.append("%s: %s not found in %s" % (ref, name, libpath))
            continue
        fp.SetReference(ref)
        fp.SetValue(comps[ref]["value"])
        board.Add(fp)
        if ref in PLACEMENT:
            x, y, rot = PLACEMENT[ref]
            fp.SetPosition(pt(x, y))
            fp.SetOrientationDegrees(rot)
            where = "placed at (%.2f, %.2f)" % (x, y)
            to_nudge.append(fp)
        elif ref in poses:
            pos, rot, flipped = poses[ref]
            fp.SetPosition(pos)
            fp.SetOrientationDegrees(rot)
            if flipped != fp.IsFlipped():
                fp.Flip(pos, False)
            where = "kept V2's position"
        else:
            fp.SetPosition(pt(PARK_X, PARK_Y0 + parked * PARK_PITCH))
            parked += 1
            where = "PARKED off-board, needs placing"
        kept.append(fp)
        added_refs.add(ref)
        added.append("%-4s %-26s %s" % (ref, comps[ref]["value"][:26], where))

    # --------------------------------------------------------------- nudge
    # Everything is on the board now, so the parts placed by hand above can be
    # shifted off their neighbours. Order matters: settle the small ones
    # against the fixed ones, not against each other.
    edges_box = board.GetBoardEdgesBoundingBox()
    for fp in to_nudge:
        ref = fp.GetReference()
        if ref in NUDGE_EXEMPT:
            continue
        others = [courtyard_box(o) for o in kept
                  if o is not fp
                  and not is_graphical(o.GetReference())
                  and pcbnew.ToMM(o.GetPosition().x) < PARK_X - 5]
        preferred = (pcbnew.ToMM(fp.GetPosition().x),
                     pcbnew.ToMM(fp.GetPosition().y))
        spot = free_spot(fp, preferred, others, edges_box)
        if spot is None:
            fp.SetPosition(pt(*preferred))
            nudged.append("%-4s no clear spot within %.0fmm, left overlapping"
                          % (ref, NUDGE_RADIUS_MM))
        elif spot[2] > 0:
            fp.SetPosition(pt(spot[0], spot[1]))
            nudged.append("%-4s moved %.2fmm to (%.2f, %.2f)"
                          % (ref, spot[2], spot[0], spot[1]))

    # ------------------------------------------------------------- relocate
    # Everything the module and the USB-C displaced, moved to the nearest
    # clear spot to the pad it serves.
    relocated = []
    for ref, (anchor_ref, anchor_pad), reason in RELOCATE:
        fp = next((f for f in kept if f.GetReference() == ref), None)
        anchor = next((f for f in kept if f.GetReference() == anchor_ref), None)
        if fp is None or anchor is None:
            relocated.append("%-4s skipped: %s not on the board"
                             % (ref, ref if fp is None else anchor_ref))
            continue
        target = pad_position(anchor, anchor_pad)
        if target is None:
            relocated.append("%-4s skipped: %s has no pad %s"
                             % (ref, anchor_ref, anchor_pad))
            continue
        others = [courtyard_box(o) for o in kept
                  if o is not fp
                  and not is_graphical(o.GetReference())
                  and pcbnew.ToMM(o.GetPosition().x) < PARK_X - 5]
        # Only move a part that is actually in trouble. Repositioning
        # unconditionally would undo a human's deliberate placement every
        # time this is re-run over a board someone has been working on.
        here = courtyard_box(fp)
        if not any(overlaps(here, other) for other in others):
            relocated.append("%-4s left where it is, nothing overlapping it"
                             % ref)
            continue
        spot = free_spot(fp, target, others, edges_box,
                         radius=RELOCATE_RADIUS_MM)
        if spot is None:
            relocated.append("%-4s NO CLEAR SPOT within %.0fmm of %s.%s"
                             % (ref, RELOCATE_RADIUS_MM, anchor_ref,
                                anchor_pad))
            continue
        fp.SetPosition(pt(spot[0], spot[1]))
        # "Nearest clear spot" is not the same as "electrically sensible". A
        # decoupling cap several millimetres from its pad is decoupling
        # nothing, so say so rather than let the distance hide in a number.
        far = "  <-- FAR, review this" if spot[2] > FAR_FROM_ANCHOR_MM else ""
        relocated.append("%-4s -> (%6.2f, %6.2f), %.2fmm from %s.%s -- %s%s"
                         % (ref, spot[0], spot[1], spot[2], anchor_ref,
                            anchor_pad, reason, far))

    # ------------------------------------------------------------------ nets
    by_ref = {fp.GetReference(): fp for fp in kept}
    assigned, unmatched = 0, []
    wanted = {}
    for name, nodes in nets.items():
        for ref, pad in nodes:
            wanted[(ref, pad)] = name

    for (ref, padname), netname in sorted(wanted.items()):
        fp = by_ref.get(ref)
        if fp is None:
            unmatched.append("%s.%s: no footprint on the board" % (ref, padname))
            continue
        pads = [p for p in fp.Pads() if p.GetPadName() == padname]
        if not pads:
            unmatched.append("%s.%s: footprint %s has no such pad" %
                             (ref, padname, fp.GetFPIDAsString()))
            continue
        net = get_net(board, netname, net_cache)
        for p in pads:
            p.SetNet(net)
            assigned += 1

    # Any pad the netlist does not mention belongs to no net.
    for fp in kept:
        ref = fp.GetReference()
        if is_graphical(ref):
            continue
        for p in fp.Pads():
            if (ref, p.GetPadName()) not in wanted:
                p.SetNet(board.FindNet(0))

    # ---------------------------------------------------------------- planes
    edges = board.GetBoardEdgesBoundingBox()
    inset = mm(PLANE_INSET_MM)
    x0, y0 = edges.GetLeft() + inset, edges.GetTop() + inset
    x1, y1 = edges.GetRight() - inset, edges.GetBottom() - inset

    for zone in list(board.Zones()):
        if zone.GetLayer() in (pcbnew.In1_Cu, pcbnew.In2_Cu):
            board.Remove(zone)

    planes = []
    for layer, netname in PLANES:
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(get_net(board, netname, net_cache))
        zone.SetIsFilled(False)
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            outline.Append(x, y)
        board.Add(zone)
        planes.append("%s pour on %s" % (netname, board.GetLayerName(layer)))

    # ------------------------------------------------------------- conflicts
    placed = [fp for fp in kept
              if not is_graphical(fp.GetReference())
              and pcbnew.ToMM(fp.GetPosition().x) < PARK_X - 5]
    now = overlap_pairs(placed)
    new_pairs = {p: d for p, d in now.items() if p not in baseline}
    conflicts = ["%-16s by %.2f mm%s" % (" <-> ".join(sorted(pair)), depth,
                                         "" if depth >= 0.2 else
                                         "   (footprint growth, ignorable)")
                 for pair, depth in sorted(new_pairs.items(),
                                           key=lambda kv: -kv[1])]
    inherited_pairs = sorted(" <-> ".join(sorted(p)) for p in
                             (set(now) & set(baseline)))
    inherited = len(inherited_pairs)
    # An overlap is only safely excusable as "V2 already had it" if this run
    # left both parts alone. Where it replaced or moved one, the pair is
    # listed for review rather than waved through under V2's name.
    touched = set(PLACEMENT) | {r for r, _, _ in RELOCATE} | set(added_refs)
    suspect = [pair for pair in inherited_pairs
               if set(pair.split(" <-> ")) & touched]

    # ----------------------------------------------------------------- report
    def section(title, items, empty="none"):
        print("== %s (%d)" % (title, len(items)))
        if not items:
            print("   %s" % empty)
        for i in items:
            print("   %s" % i)
        print()

    section("removed", removed)
    section("replaced", replaced)
    section("added", added)
    section("nudged clear of neighbours", nudged)
    section("relocated to the pad they serve", relocated)
    section("planes", planes)
    section("footprints that could not be loaded", failed)
    section("pads the netlist names but the board cannot match", unmatched)
    section("NEW courtyard overlaps -- these need a human", conflicts)
    section("V2 also had these, but this run replaced or moved a part in "
            "them -- confirm they are still V2's", suspect)
    print("inherited from V2, both parts untouched: %s"
          % ", ".join(p for p in inherited_pairs
                      if not set(p.split(" <-> ")) & touched))
    print()
    print("%d pad(s) assigned to %d net(s)" % (assigned, len(net_cache)))
    print("%d courtyard overlap(s) inherited from V2 and left alone"
          % inherited)

    if failed or unmatched:
        print("\nnot saving: the board would not match the netlist")
        return 1
    if dry_run:
        print("\ndry run, board not written")
        return 0

    board.Save(BOARD)
    print("\nwrote %s" % BOARD)
    print("NOT routed, NOT DRC-clean. Fix the overlaps above in the GUI.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
