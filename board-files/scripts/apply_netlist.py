#!/usr/bin/env python3
"""Apply the compiled netlist to the PCB, headless.

KiCad's "Import Netlist" is a GUI dialog, and `kicad-cli` has no equivalent.
This does the deterministic part of that job with `pcbnew`:

  * removes the interior cutouts from Edge.Cuts -- V2's watch-strap slots,
    which this board does not keep (see README, "Board outline")
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

import collections
import json
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "checks"))
from netlist_check import load  # noqa: E402

BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-c6.kicad_pcb")
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

# Placement for the parts this design positions, in board coordinates.
#
# This table is authoritative: it is re-applied on every run, to parts that
# are already on the board as well as to ones being added. The board is
# generated from these sources, so a position that only exists because
# somebody dragged it in the GUI is a position nothing records. Move a part
# by editing this table.
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
    "U1": (BOARD_CENTRE_X, EDGE_TOP + 2.9, 0),
    # RTC below the module, on the I2C side. V2's U6 spot is under the module
    # now.
    "U6": (80.5, 89.6, 0),
    "C15": (77.65, 89.6, 0),
    # USB-C on the right edge, on V2's micro-USB centreline so the case
    # cutout moves as little as possible.
    #
    # Rotation is 90, not 0: this footprint's solder tabs are at its local -y
    # end and the mating opening at +y, so at 0 degrees the connector faces
    # down the board and six of its pads hang over the right edge in mid-air.
    # V2's micro-USB was at 90 for the same reason. x puts the front face of
    # the housing flush with the board edge at 102.5: 102.5 - 3.725.
    "J2": (98.775, 93.68, 90),
    # The USB support parts live on the pad row, which at this pose is a
    # vertical line at x=95.105. Everything here sits just inboard of it.
    # The ESD array sits on the D+/D- pair as close to the receptacle as the
    # connector's own courtyard (left edge x=93.98) allows.
    "U3": (92.35, 93.9, 0),
    "C11": (89.75, 92.78, 0),  # VBUS bulk, shifted left to clear U3
    "R21": (92.6, 96.1, 0),   # CC1 pulldown, level with J2.A5
    "R22": (92.6, 91.2, 0),   # CC2 pulldown, level with J2.B5
    "R23": (87.5, 88.0, 0),
    # The divider row moves up 0.55mm from V2's y to clear the USB-C
    # courtyard. It stays inside the battery connector's, which is where V2
    # put it.
    "R12": (100.40, 87.28, 90),
    "R13": (99.50, 87.28, 90),
    # C16 filters the ADC tap. The top-right corner has no room left, and the
    # filter belongs at the pin it is filtering anyway, so it sits beside the
    # module's BATT_ADC pad (U1.10, IO5) rather than beside the divider that
    # drives it.
    "C16": (78.30, 80.50, 90),
    # J2's shield tabs are through-hole and stick out past the housing, and
    # the receptacle is 9.04mm wide in a 9.15mm gap. Left where V2 had them,
    # the battery connector's mounting pad and the charger's +BATT pad both
    # sit *on* a tab -- 0.067mm and 0.120mm of overlap, which is not a tight
    # clearance but a short. These three moves open the corridor:
    #
    #   U5  down 0.45    off the lower tab
    #   J3  up   0.241   off the upper tab
    #
    # SW2 does not move. J3 is pinched between the tab and that button, and at
    # the 0.2mm clearance this board used to carry the corridor was 0.052mm too
    # narrow to hold J3 with a gap at both ends -- the button had to give up
    # 0.2mm. At 0.15mm it fits: 5.430mm of corridor for 5.082mm of connector
    # leaves 0.174mm at each end. All four buttons are where V2 has them.
    "U5": (100.700, 100.630, -90),
    "J3": (96.830, 84.479, 90),
    # Listed at V2's own coordinates on purpose: an earlier revision of this
    # table moved it 0.2mm and the table is what puts it back.
    "SW2": (100.570, 79.930, -90),
    # Both sat 0.24mm and 0.29mm from the right edge against a 0.5mm rule.
    # 0.3mm inboard clears it and still leaves 0.24mm to R12, their nearest
    # neighbour. (0.2mm was not enough: DRC measures to the Edge.Cuts line,
    # which is about 0.075mm stricter than measuring to the pad bounding box.) U1's seventeen edge pads cannot be fixed this way -- the
    # module's position is set by the antenna having to overhang the edge --
    # and are accepted in checks/drc_check.py instead.
    "R16": (101.640, 87.830, -90),
    "D5": (101.440, 85.580, 90),
    "TP9": (80.43, 92.0, 0),
}

# Parts the new layout displaces, and the pad each one should sit next to.
#
# The module is 13.2 x 16.6mm where V2 had a 7 x 7mm QFN, and the USB-C is
# bigger than the micro-USB it replaces, so seven of V2's parts are now under
# something. Rather than shuffling them somewhere arbitrary, each is anchored
# to the pad it actually serves and placed at the nearest clear spot to it,
# which is what you would do by hand and is what decoupling wants anyway.
#
# None of these is a mechanical part -- the switches, the FPC and the battery
# connector are frozen at V2's coordinates and check_board.py asserts it.
RELOCATE = [
    ("C12", ("U1", "3"), "+3V3 bulk decoupling, hard against the module's 3V3 pad"),
    ("C1", ("U1", "8"), "EN capacitor, beside the EN pad"),
    ("R1", ("U1", "8"), "EN pull-up, beside the EN pad"),
    ("R7", ("U6", "2"), "RTC_INT pull-up, beside the RTC's INT pin"),
    ("Q5", ("M1", "2"), "motor driver, beside the motor pads"),
    ("U2", ("Q4", "2"), "LDO, on its own input node"),
    ("D4", ("Q4", "2"), "OR-ing diode, on the same node"),
]
RELOCATE_RADIUS_MM = 14.0
# Past this, "nearest clear spot" stops being a good answer and wants a human.
FAR_FROM_ANCHOR_MM = 5.0

# How far from its preferred spot a part with no chosen position may be
# nudged to find clear space, and the step used while searching.
#
# Nothing in PLACEMENT is nudged. Those coordinates are the design's answer
# to where the part goes, and a search that shuffles them until nothing
# overlaps will happily drag a divider resistor 7mm away from the divider to
# escape a courtyard V2 has always overlapped. Overlaps are reported instead,
# and the fix is to edit the table.
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

# VBUS is the charge path -- in at the USB-C, out through the Schottky to VSYS
# and into the TP4054 -- so it wants copper rather than a track, and it is the
# net the router has the most trouble with besides.
#
# The pour is sized to the block that carries the current, not to every pad on
# the net. A hull around all eleven VBUS pads is a 202mm^2 diagonal band that
# reaches clear across the board for D4 and D5 and swallows U2.5 -- the LDO's
# 3V3 output -- on the way, which would strand it. Bounding it to the cluster
# costs 62mm^2, traps nothing, and leaves D4 and D5 as two ordinary tracks,
# which is what they are: stubs hanging off the block.
#
# In2.Cu rather than B.Cu. Both work, but In2.Cu is already a power layer, so
# the pour costs no routing space; the same shape on B.Cu would sit straight on
# top of the J2/U3/U5/Q4 congestion, which is the most contested copper on the
# board. The cost is a split in the +3V3 plane under a corner of B.Cu -- a
# return-path discontinuity for anything crossing it, which at SPI and 12Mbps
# USB is not a real concern. Set this to pcbnew.B_Cu to move it.
VBUS_POUR_LAYER = pcbnew.In2_Cu
VBUS_POUR_NET = "VBUS"
# J2's four VBUS pads are deliberately NOT in this list.
#
# A covered pad reaches the pour through a *through* via, which blocks all
# four layers. Putting four of them in the middle of a 0.5mm-pitch USB-C pad
# field walls in A6/A7 and B6/B7 -- the differential pair -- which sit right
# beside them. The evidence is in the run history: before this pour existed
# the pair routed every time, and after it USB_DP or USB_DM failed in every
# single run. VBUS reaches the connector as ordinary track instead, which is
# what it is: the source end of the net, the same as D4 and D5.
VBUS_POUR_PADS = [("U3", "5"), ("C11", "2"), ("R14", "1"), ("Q4", "1"),
                  ("U5", "4")]
VBUS_POUR_MARGIN_MM = 1.0

# Design rules, written to the .kicad_pro as the last thing this script does.
#
# They have to be written there rather than set through pcbnew, because KiCad
# 9's Python bindings do not expose the netclasses at all -- and they have to
# be written *last*, because loading the baseline board partway through this
# script makes KiCad's settings manager flush its stale copy of the project
# over whatever the file said. That is how 0.15 silently became 0.2 again and
# a board routed at 0.15 collected 76 clearance errors against a 0.2 rule.
# check_board.py asserts these, so the next time it happens it is caught.
#
# 0.15/0.15 is not an "advanced" option -- every fab that will build a 4-layer
# board builds it, at no premium. 0.2/0.2 came from V2 and cost a quarter of
# every corridor on a board whose problem is corridors.
# Solder mask expansion, set on the board itself (this one pcbnew does expose).
#
# V2 had 0.2mm, which makes every mask opening 0.4mm wider than its pad. On
# 0.5mm-pitch parts that guarantees neighbouring openings merge, and once the
# tracks came down to 0.15mm clearance it bridged them to passing copper too:
# 155 solder-mask-bridge violations, 92 of which the router had just created.
# 0.05mm is what fabs actually ask for, and it takes all 155 to zero.
SOLDER_MASK_EXPANSION_MM = 0.05

# GND pads connect to the outer pours solidly, not through thermal spokes.
#
# This is a trade and it is worth stating. Thermal relief exists to stop a
# pour sinking heat away from a small pad during reflow; solid connections
# raise the risk of a 0402 tombstoning. Against that: GND pads on this board
# get no via down to the In1.Cu plane -- the router skips them precisely
# because the outer pours already touch them -- so the pour *is* the ground
# connection, and on twenty of them the pour could only reach with a single
# 0.15mm spoke. A ground pad hanging off one thin spoke is the worse problem.
#
# The alternative, if a fab pushes back: keep thermal relief here and give
# every GND pad its own via to In1.Cu, the way +3V3 and VBUS pads get one.
# That costs routing space this board does not currently have to spare.
GND_POUR_LAYERS = ("F.Cu", "B.Cu")
PAD_CONNECTION_SOLID = 2

# Fill islands that reach nothing are deleted rather than left floating.
#
# A pour pinched off between two tracks is still copper and still on the
# board, and KiCad counts every one as an unconnected item -- rightly, because
# a floating sliver of ground is an antenna, not a ground. route.py puts a via
# into any island big enough to take one; this deletes the rest, which is what
# the setting is for.
ISLAND_REMOVAL_ALWAYS = 0

DESIGN_RULES = {
    "clearance": 0.15,
    "track_width": 0.15,
    "diff_pair_width": 0.15,
    "diff_pair_gap": 0.2,
    "diff_pair_via_gap": 0.2,
    "via_diameter": 0.6,
    "via_drill": 0.3,
}

# How far outside a cutout's bounding box an Edge.Cuts item may still be and
# count as part of it. Only has to swallow the outline's own line width.
CUTOUT_SLOP_MM = 0.2

# Board-level rule areas inherited from V2 that this design drops, keyed by
# the bounding box V2 drew them at. Each is a no-pour region on F.Cu and B.Cu.
#
# Only the antenna strip goes. The other two stay and are listed here so it is
# on the record that they were looked at rather than missed:
#
#   72.78,104.38 .. 77.28,108.88   sits exactly on L1, the buck inductor.
#                                  Keeping pour out from under an inductor is
#                                  deliberate, and still is.
#   68.7,112.01 .. 102.5,116.11    the strip the strap slots ran through.
#                                  Harmless either way now the slots are gone,
#                                  so it is left alone.
OBSOLETE_RULE_AREAS = [
    ((68.7, 70.11, 102.5, 79.03),
     "V2's antenna clearance. V2 had a PCB antenna here; this board has a "
     "module that carries its own *.Cu keepout, and the strip now only stops "
     "ground pouring under the module, which is where it is wanted most."),
]
RULE_AREA_TOLERANCE_MM = 0.01

# V2's branding. This board is a derivative reference design, not a Watchy,
# and it should not go to a fab house carrying somebody else's name and logos
# -- so the marks come off on the grounds of it not being their product,
# before any question of DRC.
#
# It is also the single largest source of DRC errors on the board. The mask
# lettering is drawn as apertures in the solder mask, which means bare copper
# in the shape of the word, and every via, track and pad that passes beneath
# one bridges to it: 99 of the 182 solder-mask-bridge violations were items
# bridging to the word "watchy".
#
# Matched on the words and on the library, not on a count, so this stays
# correct if V2 is re-imported with more or fewer of them.
BRAND_WORDS = ("watchy", "sqfmi")
BRAND_LIBRARIES = ("Watchy:sqfmi_duck", "Watchy:sqfmi_logo")

# What is NOT branding, and stays:
#
#   Watchy:154_Epaper_Display   the display outline on B.Fab. It comes from
#                               V2's library and carries the name, but it is a
#                               mechanical outline, not a mark -- it is what
#                               stops anything being placed under the panel.
#   B.Silk "+" and "-"          battery polarity. Functional.


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def is_graphical(ref):
    return any(ref.startswith(p[:-1] if p.endswith("*") else p)
               for p in GRAPHICAL_PREFIXES)


def write_design_rules(board_path):
    """Put DESIGN_RULES into the project file beside the board.

    Last step, deliberately -- see DESIGN_RULES. Returns what changed.
    """
    project = os.path.splitext(board_path)[0] + ".kicad_pro"
    if not os.path.exists(project):
        return ["no %s -- rules not written" % os.path.basename(project)]
    with open(project) as fh:
        data = json.load(fh, object_pairs_hook=collections.OrderedDict)
    classes = data.get("net_settings", {}).get("classes", [])
    if not classes:
        return ["%s has no netclasses" % os.path.basename(project)]
    changed = []
    for cls in classes:
        if cls.get("name") != "Default":
            continue
        for key, want in DESIGN_RULES.items():
            if cls.get(key) != want:
                changed.append("%s %s -> %s" % (key, cls.get(key), want))
                cls[key] = want
    with open(project, "w") as fh:
        json.dump(data, fh, indent=2)
        fh.write("\n")
    return changed or ["already %.2fmm track / %.2fmm clearance"
                       % (DESIGN_RULES["track_width"],
                          DESIGN_RULES["clearance"])]


def convex_hull(points):
    """The smallest convex polygon containing every point."""
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def half(seq):
        stack = []
        for p in seq:
            while len(stack) > 1:
                (ax, ay), (bx, by) = stack[-2], stack[-1]
                if (bx - ax) * (p[1] - ay) - (by - ay) * (p[0] - ax) > 0:
                    break
                stack.pop()
            stack.append(p)
        return stack

    return half(pts)[:-1] + half(pts[::-1])[:-1]


def vbus_pour(board, net):
    """A pour over the block that carries the charge current.

    Built from where the pads actually are rather than from written-down
    coordinates, so it follows the placement table instead of drifting out of
    step with it the first time something moves.

    Returns the zone, or None if the pads it is meant to cover are missing.
    """
    want = set(VBUS_POUR_PADS)
    corners = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for pad in fp.Pads():
            if (ref, pad.GetPadName()) not in want:
                continue
            box = pad.GetBoundingBox()
            for x in (box.GetLeft(), box.GetRight()):
                for y in (box.GetTop(), box.GetBottom()):
                    corners.append((x, y))
    if len(corners) < 3:
        return None

    zone = pcbnew.ZONE(board)
    zone.SetLayer(VBUS_POUR_LAYER)
    zone.SetNet(net)
    zone.SetIsFilled(False)
    # Above the plane underneath it, or the plane would simply cover it.
    zone.SetAssignedPriority(1)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in convex_hull(corners):
        outline.Append(int(x), int(y))
    # A true offset, not the hull scaled up: the shape is long and thin, and
    # growing it about its centre would push the ends out while leaving the
    # sides where they were -- which is exactly where the pads need the room.
    outline.Inflate(mm(VBUS_POUR_MARGIN_MM),
                    pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, mm(0.005))
    board.Add(zone)
    return zone


def board_outline(board):
    """The board as a filled polygon, or None if Edge.Cuts does not close.

    Used to keep copper on the board. The bounding box is not enough: V2's
    outline had two interior slots, and a bounding-box test happily placed
    parts in the middle of one.
    """
    poly = pcbnew.SHAPE_POLY_SET()
    if not board.GetBoardPolygonOutlines(poly):
        return None
    return poly


def inside_outline(poly, box):
    """Is every corner of `box` on the board?"""
    if poly is None:
        return True
    corners = ((box.GetLeft(), box.GetTop()), (box.GetRight(), box.GetTop()),
               (box.GetLeft(), box.GetBottom()),
               (box.GetRight(), box.GetBottom()))
    return all(poly.Collide(pcbnew.VECTOR2I(x, y)) for x, y in corners)


def remove_obsolete_rule_areas(board):
    """Delete the board-level rule areas listed in OBSOLETE_RULE_AREAS.

    Matched on the box V2 drew, so a rule area someone adds later is never
    touched. Idempotent.
    """
    tol = mm(RULE_AREA_TOLERANCE_MM)
    removed = []
    for zone in list(board.Zones()):
        if not zone.GetIsRuleArea():
            continue
        box = zone.GetBoundingBox()
        for (x0, y0, x1, y1), why in OBSOLETE_RULE_AREAS:
            if (abs(box.GetLeft() - mm(x0)) <= tol
                    and abs(box.GetTop() - mm(y0)) <= tol
                    and abs(box.GetRight() - mm(x1)) <= tol
                    and abs(box.GetBottom() - mm(y1)) <= tol):
                board.RemoveNative(zone)
                removed.append("%.2f,%.2f .. %.2f,%.2f -- %s"
                               % (x0, y0, x1, y1, why))
                break
    return removed


def remove_v2_branding(board):
    """Delete V2's name and logos from the mask and silkscreen layers.

    Text is matched on the word and footprints on their library id, so this
    is idempotent and survives a re-import. See BRAND_WORDS for why.
    """
    removed = []
    for fp in list(board.GetFootprints()):
        if fp.GetFPIDAsString() in BRAND_LIBRARIES:
            pos = fp.GetPosition()
            removed.append("%-28s at %.2f,%.2f"
                           % (fp.GetFPIDAsString(),
                              pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)))
            board.RemoveNative(fp)

    words = collections.Counter()
    for item in list(board.GetDrawings()):
        if not isinstance(item, pcbnew.PCB_TEXT):
            continue
        text = item.GetText()
        if not any(w in text.lower() for w in BRAND_WORDS):
            continue
        words["%r on %s" % (text, board.GetLayerName(item.GetLayer()))] += 1
        board.RemoveNative(item)
    for what, n in sorted(words.items()):
        removed.append("%d x %s" % (n, what))
    return removed


def remove_interior_cutouts(board):
    """Delete the Edge.Cuts items that form holes inside the outline.

    V2 cut two watch-strap slots through the board. This design does not keep
    them -- the band is the case's problem, not the PCB's -- and they were
    directly under the module and its decoupling. Anything whose bounding box
    falls inside a hole is part of that hole; the outer outline never is.

    Idempotent: on a board with no holes this does nothing.
    """
    poly = board_outline(board)
    if poly is None or poly.OutlineCount() == 0:
        return []
    holes = []
    slop = mm(CUTOUT_SLOP_MM)
    for i in range(poly.OutlineCount()):
        for h in range(poly.HoleCount(i)):
            box = poly.Hole(i, h).BBox()
            box.Inflate(slop)
            holes.append(box)
    if not holes:
        return []
    removed = []
    for item in list(board.GetDrawings()):
        if item.GetLayer() != pcbnew.Edge_Cuts:
            continue
        box = item.GetBoundingBox()
        if any(hole.Contains(box) for hole in holes):
            board.RemoveNative(item)
            removed.append(item)
    return ["%d Edge.Cuts item(s) forming %d interior cutout(s): %s"
            % (len(removed), len(holes),
               ", ".join("x %.2f..%.2f y %.2f..%.2f"
                         % (pcbnew.ToMM(h.GetLeft()), pcbnew.ToMM(h.GetRight()),
                            pcbnew.ToMM(h.GetTop()), pcbnew.ToMM(h.GetBottom()))
                         for h in holes))] if removed else []


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


def free_spot(fp, preferred, occupied, edges, outline, radius=None):
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
            # On the board, not merely inside its bounding box.
            if not inside_outline(outline, box):
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

    # ---------------------------------------------------------------- outline
    # Before anything is placed: the outline decides where copper may go, and
    # the plane geometry below is derived from it.
    cutouts = remove_interior_cutouts(board)
    outline = board_outline(board)
    keepouts = remove_obsolete_rule_areas(board)
    branding = remove_v2_branding(board)

    # V2 packs its 1uF panel caps and its Schottky trio close enough that
    # their courtyards already overlap. Those are upstream's business, not
    # this board's, so the report below is a diff against the board as it
    # arrived, not an absolute count.
    if not os.path.exists(BASELINE):
        print("missing %s -- the overlap report needs V2's untouched layout "
              "to diff against" % BASELINE, file=sys.stderr)
        return 2
    # Bound to a name on purpose: KiCad 9's bindings hand back a bare
    # SwigPyObject if the BOARD is collected while its iterator is alive.
    baseline_board = pcbnew.LoadBoard(BASELINE)
    baseline = overlap_pairs(
        fp for fp in baseline_board.GetFootprints()
        if not is_graphical(fp.GetReference()))

    # ---------------------------------------------------------------- delete
    # pcbnew's footprint iterator does not survive a Remove(), so snapshot the
    # board once and track the surviving set ourselves from here on.
    # RemoveNative, not Remove, throughout: Remove() hands ownership of the
    # item to Python, and KiCad 9's bindings then free it in a way that
    # corrupts the SWIG type registry -- the next LoadBoard comes back as a
    # bare SwigPyObject. RemoveNative deletes it in C++ where it belongs.
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
            board.RemoveNative(fp)
            continue
        want = comps[ref]["footprint"]
        have = fp.GetFPIDAsString()
        if have not in (want, want.split(":")[-1]):
            replaced.append("%s: %s -> %s" % (ref, have, want))
            poses[ref] = (fp.GetPosition(), fp.GetOrientationDegrees(),
                          fp.IsFlipped())
            board.RemoveNative(fp)
            continue
        kept.append(fp)

    present = {fp.GetReference() for fp in kept}

    # ------------------------------------------------------------------- add
    # -------------------------------------------------------- reposition
    # Parts named in PLACEMENT that are already on the board. Applied before
    # the adds so the nudge pass below settles everything against the final
    # positions, not the old ones.
    repositioned = []
    to_nudge = []
    for fp in kept:
        ref = fp.GetReference()
        if ref not in PLACEMENT:
            continue
        x, y, rot = PLACEMENT[ref]
        was = (pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y),
               fp.GetOrientationDegrees())
        if (abs(was[0] - x) < 1e-6 and abs(was[1] - y) < 1e-6
                and abs(was[2] - rot) < 1e-6):
            continue
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        to_nudge.append(fp)
        repositioned.append("%-4s (%.2f, %.2f) rot %.0f -> (%.2f, %.2f) rot %.0f"
                            % (ref, was[0], was[1], was[2], x, y, rot))

    added, failed, nudged, added_refs = [], [], [], set()
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
        if ref in NUDGE_EXEMPT or ref in PLACEMENT:
            continue
        others = [courtyard_box(o) for o in kept
                  if o is not fp
                  and not is_graphical(o.GetReference())
                  and pcbnew.ToMM(o.GetPosition().x) < PARK_X - 5]
        preferred = (pcbnew.ToMM(fp.GetPosition().x),
                     pcbnew.ToMM(fp.GetPosition().y))
        spot = free_spot(fp, preferred, others, edges_box, outline)
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
        spot = free_spot(fp, target, others, edges_box, outline,
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
        if zone.GetIsRuleArea():
            continue
        # The inner layers are rebuilt from scratch every run, and so is the
        # VBUS pour wherever it lives -- otherwise moving it to another layer
        # leaves the old one behind and the net is poured twice.
        if (zone.GetLayer() in (pcbnew.In1_Cu, pcbnew.In2_Cu)
                or zone.GetNetname() == VBUS_POUR_NET):
            board.RemoveNative(zone)

    planes = []
    for layer, netname in PLANES:
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(get_net(board, netname, net_cache))
        zone.SetIsFilled(False)
        zone.SetAssignedPriority(0)
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            outline.Append(x, y)
        board.Add(zone)
        planes.append("%s pour on %s" % (netname, board.GetLayerName(layer)))

    pour = vbus_pour(board, get_net(board, VBUS_POUR_NET, net_cache))
    if pour is None:
        planes.append("VBUS pour NOT built -- its pads are not on the board")
    else:
        planes.append("%s pour on %s, %.1f mm2 over %d pad(s), the rest routed"
                      % (VBUS_POUR_NET,
                         board.GetLayerName(VBUS_POUR_LAYER),
                         pcbnew.ToMM(pcbnew.ToMM(pour.Outline().Area())),
                         len(VBUS_POUR_PADS)))

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

    section("interior cutouts removed from Edge.Cuts", cutouts)
    section("repositioned to the placement table", repositioned)
    section("obsolete rule areas removed", keepouts)
    section("V2 branding removed", branding)
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

    for zone in board.Zones():
        if zone.GetIsRuleArea() or zone.GetNetname() != "GND":
            continue
        if board.GetLayerName(zone.GetLayer()) not in GND_POUR_LAYERS:
            continue
        zone.SetPadConnection(PAD_CONNECTION_SOLID)

    for zone in board.Zones():
        if zone.GetIsRuleArea():
            continue
        zone.SetIslandRemovalMode(ISLAND_REMOVAL_ALWAYS)

    settings = board.GetDesignSettings()
    if settings.m_SolderMaskExpansion != mm(SOLDER_MASK_EXPANSION_MM):
        print("solder mask expansion: %.3f -> %.3f mm"
              % (pcbnew.ToMM(settings.m_SolderMaskExpansion),
                 SOLDER_MASK_EXPANSION_MM))
        settings.m_SolderMaskExpansion = mm(SOLDER_MASK_EXPANSION_MM)

    board.Save(BOARD)
    for line in write_design_rules(BOARD):
        print("design rules: %s" % line)
    print("\nwrote %s" % BOARD)
    print("NOT routed, NOT DRC-clean. Fix the overlaps above in the GUI.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
