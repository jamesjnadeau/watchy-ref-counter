#!/usr/bin/env python3
"""Route the board, headless.

KiCad has no autorouter and no routing API. This is a maze router written
against `pcbnew`, so that the copper on this board is generated from the
netlist the same way the placement is, and `git diff` on this file says what
a routing change did.

What it does, in order:

  * deletes every track and via on the board. V2's routing is still there
    otherwise, and it was routed for V2's netlist -- a PICO-D4 pinout, a
    micro-USB and a PCF8563. Left in place it shorts nets rather than
    connecting them.
  * unfills every zone, so nothing downstream reads a fill computed for a
    board that no longer exists
  * drops a via in each of the module's thermal pads, which no pour can reach
    and no via can sit beside
  * ties every +3V3 pad to the In2.Cu plane, and stitches GND between F.Cu,
    In1.Cu and B.Cu on a coarse grid
  * routes the signal nets on F.Cu and B.Cu with A* over a 0.1mm grid
  * refills the zones and reports what is still unconnected

Power is not routed as copper: GND and +3V3 are planes, and a pad reaches
them through a via rather than a track. That is what buys the space to route
the rest on two layers.

    ./scripts/route.py             # routes and writes the board
    ./scripts/route.py --dry-run   # report only

Needs KiCad's python bindings, so the system interpreter that ships them.
Deterministic: same board in, same copper out.
"""

import os
import sys
import heapq
from array import array

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BOARD = os.path.join(ROOT, "elec/layout/default/watchy-ref-c6.kicad_pcb")

# The grid the search runs on. 0.1mm is fine enough to thread a 0.2mm track
# through a 0.5mm-pitch connector fanout and coarse enough that the whole
# board is only ~157k cells a layer.
GRID_MM = 0.1

# 0.15/0.15. Every fab that will build a 4-layer board at all will build this;
# 0.2/0.2 was a conservative default inherited from V2 and it costs a third of
# every corridor on a board whose problem is corridors.
TRACK_MM = 0.15
CLEAR_MM = 0.15
VIA_D_MM = 0.6
VIA_DRILL_MM = 0.3
EDGE_CLEAR_MM = 0.5
# Board setup's hole-to-copper rule. Wider than the copper clearance, so holes
# get their own stamp rather than riding on the pad halo.
HOLE_CLEAR_MM = 0.25

# What a via costs, in grid cells, against the straight-line alternative.
# 25 cells is 2.5mm. This was swept: 6, 10, 16, 25, 35, 50 and 80 all route,
# and 25 routes the most. Cheap vias look attractive because B.Cu is nearly
# empty, but a via blocks a 1.0mm disc on *both* layers where a track blocks
# a 0.4mm ribbon on one, so a router that vias freely spends more board than
# it saves.
VIA_COST_CELLS = 25

# Diagonal steps are allowed, so tracks come out at 45 degrees rather than
# in staircases.
SQRT2 = 1.41421356

# Cells one search may expand before it gives up. A route here is a few
# hundred cells, but a search that has to work its way around most of the
# board legitimately expands tens of thousands. 250,000 was measured: below
# it, searches that would have succeeded get cut off; above it, the failures
# that remain are real rather than budgetary.
MAX_EXPANSIONS = 250000

# What crossing an already-routed net costs while looking for who to rip up.
# High enough that a clear path always wins, low enough that the search will
# cross several nets rather than give up.
SOFT_PENALTY = 40

# The scout that looks for blockers may cross other nets, and every crossing
# multiplies the frontier, so it needs a far larger budget than a normal
# search. It runs a handful of times per board, so it can afford one.
SCOUT_EXPANSIONS = 600000

# How many rip-up rounds to run. Each one takes the nets standing in a failed
# net's way, deletes their copper, routes the failed net through the gap and
# then puts the others back somewhere else.
RIPUP_ROUNDS = 12

# Which nets have pours, and which of their pads those pours actually reach,
# are both read off the board rather than written down here -- see plane_cover.
# The router follows whatever apply_netlist.py laid down instead of having to
# be kept in step with it by hand.
ROUTE_LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)

# GND is a pour on F.Cu, In1.Cu and B.Cu. The pour connects the pads; these
# vias connect the pours to each other, on a grid, wherever one fits.
STITCH_PITCH_MM = 3.0

# A via may sit inside a pad of its own net only when the pad is at least
# this big. That is the module's thermal array (1.2mm pads on a 1.65mm
# pitch), where the gaps are too narrow for a pour to enter and too narrow
# for a via to sit beside. Espressif's own reference layout does the same.
# Anything smaller keeps vias out of its pads, where they would wick solder.
VIA_IN_PAD_MIN_MM = 1.0


def mm(v):
    return pcbnew.FromMM(v)


def to_mm(v):
    return pcbnew.ToMM(v)


class Grid:
    """Occupancy for the routable layers, one cell per GRID_MM square.

    Two grids per layer, because a track and a via do not need the same room:

      `t`  a cell is blocked for a *track* centreline
      `v`  a cell is blocked for a *via* centre

    Each obstacle is stamped at radius (its own half-size + clearance + the
    half-size of the thing being placed), so a free cell means the object may
    be centred there and still meet clearance. Values are net codes: 0 free,
    -1 permanently blocked, n reserved by net n and passable only by net n.
    """

    def __init__(self, box):
        self.x0 = box.GetLeft()
        self.y0 = box.GetTop()
        self.step = mm(GRID_MM)
        self.w = int(box.GetWidth() / self.step) + 2
        self.h = int(box.GetHeight() / self.step) + 2
        self.n = self.w * self.h
        self.t = {layer: array("i", bytes(4 * self.n)) for layer in ROUTE_LAYERS}
        self.v = {layer: array("i", bytes(4 * self.n)) for layer in ROUTE_LAYERS}

    def snapshot(self):
        """A copy of the occupancy, to roll back to when ripping up."""
        return ({l: array("i", a) for l, a in self.t.items()},
                {l: array("i", a) for l, a in self.v.items()})

    def restore(self, snap):
        for l, a in snap[0].items():
            self.t[l][:] = a
        for l, a in snap[1].items():
            self.v[l][:] = a

    def ix(self, x):
        return int(round((x - self.x0) / self.step))

    def iy(self, y):
        return int(round((y - self.y0) / self.step))

    def x(self, ix):
        return self.x0 + ix * self.step

    def y(self, iy):
        return self.y0 + iy * self.step

    def cell(self, ix, iy):
        return iy * self.w + ix

    def inside(self, ix, iy):
        return 0 <= ix < self.w and 0 <= iy < self.h

    @staticmethod
    def _put(arr, idx, code, merge):
        """Claim a cell for `code`.

        With `merge`, a cell claimed by two different nets becomes -1 rather
        than belonging to whichever was stamped last. That distinction is the
        whole correctness of the router: clearance halos overlap constantly,
        and last-writer-wins hands the loser's exclusion zone to the winner,
        which then routes through it. It put vias into other nets' pads.
        """
        if not merge:
            arr[idx] = code
            return
        cur = arr[idx]
        if cur == 0:
            arr[idx] = code
        elif cur != code:
            arr[idx] = -1

    def stamp_box(self, arr, left, top, right, bottom, code, merge=False):
        """Mark a rectangle of cells, rounding outwards."""
        i0, i1 = self.ix(left), self.ix(right)
        j0, j1 = self.iy(top), self.iy(bottom)
        for j in range(max(0, j0), min(self.h - 1, j1) + 1):
            base = j * self.w
            for i in range(max(0, i0), min(self.w - 1, i1) + 1):
                self._put(arr, base + i, code, merge)

    def stamp_rrect(self, arr, left, top, right, bottom, radius, code,
                    merge=False):
        """Mark every cell within `radius` of a rectangle.

        Not the bounding box grown by `radius` -- the true offset shape, with
        rounded corners. The difference is only the corners, and the corners
        are the whole game on a 0.5mm-pitch connector: a pad in a row that
        close can only be escaped along its own axis, and squaring off the
        neighbours' clearance closes exactly that lane. It is also simply
        wrong, because clearance is measured between shapes, not between
        bounding boxes.
        """
        i0, i1 = self.ix(left - radius), self.ix(right + radius)
        j0, j1 = self.iy(top - radius), self.iy(bottom + radius)
        rr = radius * radius
        for j in range(max(0, j0), min(self.h - 1, j1) + 1):
            y = self.y(j)
            dy = top - y if y < top else (y - bottom if y > bottom else 0)
            base = j * self.w
            row = rr - dy * dy
            if row < 0:
                continue
            for i in range(max(0, i0), min(self.w - 1, i1) + 1):
                x = self.x(i)
                dx = left - x if x < left else (x - right if x > right else 0)
                if dx * dx <= row:
                    self._put(arr, base + i, code, merge)

    def stamp_seg(self, arr, ax, ay, bx, by, radius, code, merge=False):
        """Mark every cell within `radius` of the segment a->b."""
        steps = max(abs(bx - ax), abs(by - ay)) // self.step + 1
        for s in range(int(steps) + 1):
            f = s / steps if steps else 0
            self.stamp_disc(arr, ax + (bx - ax) * f, ay + (by - ay) * f,
                            radius, code, merge)

    def stamp_disc(self, arr, cx, cy, radius, code, merge=False):
        r = int(radius / self.step) + 1
        ci, cj = self.ix(cx), self.iy(cy)
        rr = r * r
        for dj in range(-r, r + 1):
            j = cj + dj
            if not 0 <= j < self.h:
                continue
            base = j * self.w
            span = int((rr - dj * dj) ** 0.5) if abs(dj) <= r else 0
            for di in range(-span, span + 1):
                i = ci + di
                if 0 <= i < self.w:
                    self._put(arr, base + i, code, merge)


def board_polygon(board):
    poly = pcbnew.SHAPE_POLY_SET()
    if not board.GetBoardPolygonOutlines(poly):
        raise SystemExit("Edge.Cuts does not close -- cannot route")
    return poly


def build_grid(board):
    """Occupancy with the board edge, the keepouts and every pad stamped in."""
    grid = Grid(board.GetBoardEdgesBoundingBox())
    poly = board_polygon(board)

    # Off-board, and the strip along the edge no copper may enter. Deflating
    # the outline and testing cell centres against it does both at once.
    for what, extra in (("t", TRACK_MM / 2), ("v", VIA_D_MM / 2)):
        room = pcbnew.SHAPE_POLY_SET(poly)
        room.Deflate(mm(EDGE_CLEAR_MM + extra),
                     pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, mm(0.01))
        arrs = getattr(grid, what)
        for j in range(grid.h):
            y = grid.y(j)
            row = j * grid.w
            for i in range(grid.w):
                if not room.Collide(pcbnew.VECTOR2I(grid.x(i), y)):
                    for layer in ROUTE_LAYERS:
                        arrs[layer][row + i] = -1

    # Rule areas that forbid tracks or vias -- the module's antenna keepout.
    for zone in board.Zones():
        if not zone.GetIsRuleArea():
            continue
        box = zone.GetBoundingBox()
        for layer in ROUTE_LAYERS:
            if not zone.IsOnLayer(layer):
                continue
            if zone.GetDoNotAllowTracks():
                grid.stamp_box(grid.t[layer], box.GetLeft(), box.GetTop(),
                               box.GetRight(), box.GetBottom(), -1)
            if zone.GetDoNotAllowVias() or zone.GetDoNotAllowTracks():
                grid.stamp_box(grid.v[layer], box.GetLeft(), box.GetTop(),
                               box.GetRight(), box.GetBottom(), -1)

    # Pads, in two passes.
    #
    # A pad blocks every other net within (its half-size + clearance + the
    # half-size of whatever is being placed). Those halos overlap neighbouring
    # pads, so stamping each pad's halo and body together lets the last pad
    # stamped take ownership of its neighbour's copper -- and then that
    # neighbour's own net cannot reach its own pad. Halos first, bodies
    # second, so a pad's interior always ends up owned by the pad.
    pads = [pad for fp in board.GetFootprints() for pad in fp.Pads()]
    for phase in ("halo", "body"):
        for pad in pads:
            code = pad.GetNetCode() or -1
            box = pad.GetBoundingBox()
            big = (min(to_mm(pad.GetSize().x), to_mm(pad.GetSize().y))
                   >= VIA_IN_PAD_MIN_MM)
            for layer in ROUTE_LAYERS:
                if not pad.IsOnLayer(layer):
                    continue
                for what, extra, may_sit_in_pad in (
                        ("t", TRACK_MM / 2, True),
                        ("v", VIA_D_MM / 2, big)):
                    arr = getattr(grid, what)[layer]
                    if phase == "halo":
                        grid.stamp_rrect(arr, box.GetLeft(), box.GetTop(),
                                         box.GetRight(), box.GetBottom(),
                                         mm(CLEAR_MM + extra), code,
                                         merge=True)
                        continue
                    if not may_sit_in_pad:
                        # A via in this pad would wick solder off it.
                        grid.stamp_box(arr, box.GetLeft(), box.GetTop(),
                                       box.GetRight(), box.GetBottom(), -1)
                        continue
                    # The pad's own net owns the part of the pad an object of
                    # this size can sit in without hanging over the edge.
                    grid.stamp_box(arr, *inner(box, mm(extra)), code)

    # Unplated holes, on every layer, blocking every net.
    #
    # An NPTH pad is a hole with no copper, so it is on no copper layer, so
    # the loop above skips it completely -- which is how tracks ended up
    # inside J2's mounting holes. Plated holes need no equivalent: their own
    # copper is stamped above and its halo already covers the drill.
    for pad in pads:
        if pad.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
            continue
        drill = pad.GetDrillSize()
        radius = max(to_mm(drill.x), to_mm(drill.y)) / 2.0
        if radius <= 0:
            continue
        pos = pad.GetPosition()
        for what, extra in (("t", TRACK_MM / 2), ("v", VIA_D_MM / 2)):
            arrs = getattr(grid, what)
            for layer in ROUTE_LAYERS:
                grid.stamp_disc(arrs[layer], pos.x, pos.y,
                                mm(radius + HOLE_CLEAR_MM + extra), -1)
    return grid


def inner(box, margin):
    """The box shrunk by `margin`, never smaller than its centre point."""
    left = box.GetLeft() + margin
    right = box.GetRight() - margin
    top = box.GetTop() + margin
    bottom = box.GetBottom() - margin
    if left > right:
        left = right = (box.GetLeft() + box.GetRight()) // 2
    if top > bottom:
        top = bottom = (box.GetTop() + box.GetBottom()) // 2
    return left, top, right, bottom


_PAD_CELLS = {}


def pad_cells(grid, pad):
    """Cells a track of this pad's net may start from: the pad's own copper.

    The pad's real outline deflated by half a track width, not its bounding
    box. Test points are 1mm circles and the vibration motor's pad is an oval,
    and the corners of their bounding boxes are outside the copper -- a track
    that ended in one stopped short of the pad it was routed to. The router
    called those nets connected; KiCad's ratsnest did not, which is where
    TP1, TP2, TP9 and M1 came from.
    """
    key = pad.m_Uuid.AsString()
    hit = _PAD_CELLS.get(key)
    if hit is None:
        layer = (pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu)
                 else pcbnew.B_Cu)
        shape = pcbnew.SHAPE_POLY_SET(pad.GetEffectivePolygon(layer))
        shape.Deflate(mm(TRACK_MM / 2),
                      pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, mm(0.01))
        box = pad.GetBoundingBox()
        hit = []
        for j in range(grid.iy(box.GetTop()), grid.iy(box.GetBottom()) + 1):
            for i in range(grid.ix(box.GetLeft()), grid.ix(box.GetRight()) + 1):
                if not grid.inside(i, j):
                    continue
                if shape.Collide(pcbnew.VECTOR2I(grid.x(i), grid.y(j))):
                    hit.append((i, j))
        if not hit:
            # Smaller than a track is wide. Its centre will have to do.
            pos = pad.GetPosition()
            hit = [(grid.ix(pos.x), grid.iy(pos.y))]
        _PAD_CELLS[key] = hit
    return hit


NEIGHBOURS = ((1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
              (1, 1, SQRT2), (1, -1, SQRT2), (-1, 1, SQRT2), (-1, -1, SQRT2))


def astar(grid, code, sources, targets, goal_is_via=False, base=None,
          crossed=None, limit=None):
    """Cheapest path from any source cell to any target cell.

    With `base` -- the occupancy as it was before anything was routed -- a
    cell that is blocked only by another net's *routing* becomes passable at
    SOFT_PENALTY, and the net that owns it is recorded in `crossed`. Nothing
    is written through those cells; the point is to find out who is in the
    way so they can be ripped up and re-routed, which is the difference
    between a router that stalls at 90% and one that finishes.
    """
    """Cheapest path from any source cell to any target cell.

    States are (ix, iy, layer). Returns a list of states, or None. `sources`
    and `targets` are {(ix, iy, layer)} sets; with `goal_is_via` the search
    instead stops at the first cell where a via may legally be dropped.
    """
    layers = ROUTE_LAYERS
    if targets:
        # Octile distance to the targets' bounding box. Measuring against
        # every target cell instead is exact but costs O(cells) on every
        # expansion, and a pad is dozens of cells.
        ti0 = min(i for i, j, l in targets)
        ti1 = max(i for i, j, l in targets)
        tj0 = min(j for i, j, l in targets)
        tj1 = max(j for i, j, l in targets)

        def h(i, j):
            dx = ti0 - i if i < ti0 else (i - ti1 if i > ti1 else 0)
            dy = tj0 - j if j < tj0 else (j - tj1 if j > tj1 else 0)
            return (dx + dy) - (2 - SQRT2) * (dx if dx < dy else dy)
    else:
        def h(i, j):
            return 0.0

    def via_ok(i, j):
        c = grid.cell(i, j)
        for layer in layers:
            val = grid.v[layer][c]
            if val != 0 and val != code:
                return False
        return True

    def cost_of(layer, i, j):
        """None if impassable, else the extra cost of entering."""
        val = grid.t[layer][grid.cell(i, j)]
        if val == 0 or val == code:
            return 0
        if base is None or val == -1:
            return None
        if base[0][layer][grid.cell(i, j)] in (0, code):
            return SOFT_PENALTY      # someone else's track, not their pad
        return None

    open_heap = []
    expanded = 0
    best = {}
    for state in sources:
        best[state] = 0.0
        heapq.heappush(open_heap, (h(state[0], state[1]), 0.0, state, None))
    came = {}

    while open_heap:
        _, g, state, parent = heapq.heappop(open_heap)
        if state in came:
            continue
        came[state] = parent
        expanded += 1
        if expanded > (limit or MAX_EXPANSIONS):
            # A route on this board is short. Past this the search is not
            # finding one, it is flooding the board to prove it cannot -- and
            # that is the difference between a pass taking a minute and an
            # hour.
            return None
        i, j, layer = state
        if goal_is_via:
            if via_ok(i, j):
                return unwind(came, state)
        elif state in targets:
            path = unwind(came, state)
            if crossed is not None:
                for i2, j2, l2 in path:
                    val = grid.t[l2][grid.cell(i2, j2)]
                    if val not in (0, code, -1):
                        crossed.add(val)
            return path

        for di, dj, step in NEIGHBOURS:
            ni, nj = i + di, j + dj
            if not grid.inside(ni, nj):
                continue
            nxt = (ni, nj, layer)
            if nxt in came:
                continue
            extra = cost_of(layer, ni, nj)
            if extra is None:
                continue
            if di and dj:
                # A diagonal step passes between the two cells beside it. If
                # either is someone else's, this track clips their clearance
                # on the way past -- and if the other net made the opposite
                # diagonal move, the two cross without ever sharing a cell.
                # KiCad calls that "tracks crossing"; it is a short.
                side_a = cost_of(layer, i + di, j)
                side_b = cost_of(layer, i, j + dj)
                if side_a is None or side_b is None:
                    continue
                extra = max(extra, side_a, side_b)
            ng = g + step + extra
            if ng < best.get(nxt, 1e18):
                best[nxt] = ng
                heapq.heappush(open_heap, (ng + h(ni, nj), ng, nxt, state))

        if not goal_is_via and via_ok(i, j):
            for layer2 in layers:
                if layer2 == layer:
                    continue
                nxt = (i, j, layer2)
                if nxt in came:
                    continue
                extra = cost_of(layer2, i, j)
                if extra is None:
                    continue
                ng = g + VIA_COST_CELLS + extra
                if ng < best.get(nxt, 1e18):
                    best[nxt] = ng
                    heapq.heappush(open_heap, (ng + h(i, j), ng, nxt, state))
    return None


def unwind(came, state):
    path = []
    while state is not None:
        path.append(state)
        state = came[state]
    path.reverse()
    return path


def path_to_copper(board, grid, path, net, tracks, vias):
    """Turn a list of states into PCB_TRACKs and PCB_VIAs on the board."""
    run = [path[0]]
    for state in path[1:]:
        if state[2] != run[-1][2]:
            emit_run(board, grid, run, net, tracks)
            add_via(board, grid, state[0], state[1], net, vias)
            run = [state]
            continue
        run.append(state)
    emit_run(board, grid, run, net, tracks)


def direction(a, b):
    return (b[0] - a[0], b[1] - a[1])


def emit_run(board, grid, run, net, tracks):
    """One layer's worth of a path, as few straight segments as possible."""
    if len(run) < 2:
        return
    layer = run[0][2]
    start = run[0]
    heading = direction(run[0], run[1])
    for k in range(1, len(run)):
        here = direction(run[k - 1], run[k])
        if here != heading:
            add_track(board, grid, start, run[k - 1], layer, net, tracks)
            start = run[k - 1]
            heading = here
    add_track(board, grid, start, run[-1], layer, net, tracks)


def add_track(board, grid, a, b, layer, net, tracks):
    if a[:2] == b[:2]:
        return
    t = pcbnew.PCB_TRACK(board)
    ax, ay = grid.x(a[0]), grid.y(a[1])
    bx, by = grid.x(b[0]), grid.y(b[1])
    t.SetStart(pcbnew.VECTOR2I(ax, ay))
    t.SetEnd(pcbnew.VECTOR2I(bx, by))
    t.SetWidth(mm(TRACK_MM))
    t.SetLayer(layer)
    t.SetNet(net)
    board.Add(t)
    tracks.append(t)
    code = net.GetNetCode()
    grid.stamp_seg(grid.t[layer], ax, ay, bx, by,
                   mm(TRACK_MM / 2 + CLEAR_MM + TRACK_MM / 2), code, merge=True)
    grid.stamp_seg(grid.v[layer], ax, ay, bx, by,
                   mm(TRACK_MM / 2 + CLEAR_MM + VIA_D_MM / 2), code, merge=True)


def add_via(board, grid, i, j, net, vias):
    v = pcbnew.PCB_VIA(board)
    x, y = grid.x(i), grid.y(j)
    v.SetPosition(pcbnew.VECTOR2I(x, y))
    # KiCad 9 vias are per-layer sized; SetWidth without a layer asserts.
    for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        v.SetWidth(layer, mm(VIA_D_MM))
    v.SetDrill(mm(VIA_DRILL_MM))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    # Tented both sides. Half of these sit in the middle of a pad field or
    # inside a thermal pad, where a bare mask aperture would bridge to the
    # pad beside it and wick solder off the joint on the way. It also takes
    # 52 solder-mask-bridge violations off the DRC report, which were the
    # router's own doing rather than the footprints'.
    v.SetFrontTentingMode(pcbnew.TENTING_MODE_TENTED)
    v.SetBackTentingMode(pcbnew.TENTING_MODE_TENTED)
    v.SetNet(net)
    board.Add(v)
    vias.append(v)
    code = net.GetNetCode()
    for layer in ROUTE_LAYERS:
        grid.stamp_disc(grid.t[layer], x, y,
                        mm(VIA_D_MM / 2 + CLEAR_MM + TRACK_MM / 2), code,
                        merge=True)
        grid.stamp_disc(grid.v[layer], x, y,
                        mm(VIA_D_MM / 2 + CLEAR_MM + VIA_D_MM / 2), code,
                        merge=True)
    return v


# Pads a pour physically covers but which must not be given a via.
#
# A pad reaches its pour through a *through* via, and a through via blocks all
# four layers. J2's VBUS pads sit in the middle of a 0.5mm-pitch USB-C field
# with A6/A7 and B6/B7 -- the differential pair -- immediately beside them, and
# the VBUS pour's outline runs under them because the parts it does serve lie
# on both sides. Dropping vias there walls the pair in: before this pour
# existed the pair routed in every run, and afterwards USB_DP or USB_DM failed
# in every one.
#
# Marking them uncovered is exactly right in both places it matters: no via is
# dropped, and the net is handed to the router to reach with ordinary track --
# which is what it should be at the source end, the same as D4 and D5.
PLANE_VIA_EXCLUDE = {("J2", "A4"), ("J2", "A9"), ("J2", "B4"), ("J2", "B9")}


def plane_cover(board):
    """{net name: (layer name, {(ref, pad) the pour reaches})} for poured nets.

    A pour is only worth anything to the router where it actually covers the
    pad. The full-board planes cover all of theirs, so this changes nothing for
    GND or +3V3; the VBUS pour is bounded to the block that carries the charge
    current, so the two pads outside it -- D4 and D5, stubs hanging off that
    block -- come back as ordinary routing work rather than being silently
    dropped on the floor.
    """
    def key(pad):
        # Pad identity, not (ref, name): J2 has four separate shield pads all
        # called S1, and keying by name would call every one of them covered
        # the moment one was.
        return pad.m_Uuid.AsString()

    zones = {}
    for zone in board.Zones():
        if zone.GetIsRuleArea() or not zone.GetNetname():
            continue
        zones.setdefault(zone.GetNetname(), []).append(zone)

    cover = {}
    for name, owned in zones.items():
        layer = board.GetLayerName(owned[0].GetLayer())
        pads = set()
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() != name:
                    continue
                if (fp.GetReference(), pad.GetPadName()) in PLANE_VIA_EXCLUDE:
                    continue
                pos = pad.GetPosition()
                if any(z.Outline().Contains(pos) for z in owned):
                    pads.add(key(pad))
        cover[name] = (layer, pads)
    return cover



# Routed first, because they are the nets whose geometry matters and the
# board is emptiest now. The differential pair goes down before anything can
# force it apart, and the charge pump before its loop can be crossed.
PRIORITY_NETS = ("USB_DP", "USB_DM", "CP_POS", "CP_NEG", "GDR", "RESE")


def collect_nets(board):
    """{net name: [(footprint, pad), ...]} for every net with copper."""
    nets = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            name = pad.GetNetname()
            if name:
                nets.setdefault(name, []).append((fp, pad))
    return nets


def plane_pads(board, cover, big_only):
    """Pads a pour reaches, optionally only the ones big enough for a via.

    A pad on a poured net that the pour does not actually cover is not a plane
    pad at all -- it still has to be routed -- so it is not returned here.
    """
    out = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for pad in fp.Pads():
            name = pad.GetNetname()
            if name not in cover:
                continue
            if pad.m_Uuid.AsString() not in cover[name][1]:
                continue
            big = (min(to_mm(pad.GetSize().x), to_mm(pad.GetSize().y))
                   >= VIA_IN_PAD_MIN_MM)
            if big_only == big:
                out.append((ref, name, pad))
    return out


def stitch_ground(board, grid, vias):
    """Vias on a coarse grid tying the GND pours on F.Cu, In1.Cu and B.Cu.

    GND reaches its pads through the pours, so what is left is making the
    pours one conductor. Anywhere a via fits, one goes.
    """
    net = board.FindNet("GND")
    if net is None:
        return 0
    pitch = int(mm(STITCH_PITCH_MM) / grid.step)
    code = net.GetNetCode()
    placed = 0
    for j in range(pitch // 2, grid.h, pitch):
        for i in range(pitch // 2, grid.w, pitch):
            if all(grid.v[layer][grid.cell(i, j)] in (0, code)
                   for layer in ROUTE_LAYERS):
                add_via(board, grid, i, j, net, vias)
                placed += 1
    return placed


def stitch_islands(board, grid, vias, delete=False):
    """Give every GND pour island of its own a via, if it has none.

    The 3mm stitching grid ties the pours together where a via happens to fit
    on it. It does not notice an island that the grid stepped over -- a strip
    of F.Cu ground pinched off between two tracks is still ground, and still
    floating, and the ratsnest counts it. This walks the filled polygons
    afterwards and puts a via in any island that has no copper of its own net
    already reaching another layer.
    """
    net = board.FindNet("GND")
    if net is None:
        return [], set()
    code = net.GetNetCode()
    anchors = [(v.GetPosition().x, v.GetPosition().y) for v in board.GetTracks()
               if isinstance(v, pcbnew.PCB_VIA) and v.GetNetCode() == code]
    anchors += [(p.GetPosition().x, p.GetPosition().y)
                for fp in board.GetFootprints() for p in fp.Pads()
                if p.GetNetCode() == code and p.GetAttribute() in
                (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]

    added = []
    stranded = set()
    drop = []                      # (zone, layer, outline index); ZONE is not hashable
    for zone in board.Zones():
        if zone.GetIsRuleArea() or zone.GetNetCode() != code:
            continue
        layer = zone.GetLayer()
        if layer not in ROUTE_LAYERS:
            continue
        filled = zone.GetFilledPolysList(layer)
        for n in range(filled.OutlineCount()):
            island = pcbnew.SHAPE_POLY_SET()
            island.AddOutline(filled.Outline(n))
            if any(island.Collide(pcbnew.VECTOR2I(x, y)) for x, y in anchors):
                continue
            box = island.BBox()
            spot = None
            for j in range(grid.iy(box.GetTop()), grid.iy(box.GetBottom()) + 1):
                for i in range(grid.ix(box.GetLeft()),
                               grid.ix(box.GetRight()) + 1):
                    if not grid.inside(i, j):
                        continue
                    if not all(grid.v[lay][grid.cell(i, j)] in (0, code)
                               for lay in ROUTE_LAYERS):
                        continue
                    if island.Collide(pcbnew.VECTOR2I(grid.x(i), grid.y(j))):
                        spot = (i, j)
                        break
                if spot:
                    break
            if delete:
                # Second pass: everything still isolated after the re-fill.
                # Any pad sitting in it loses its only copper when it goes, so
                # it gets a via down to the In1.Cu plane first -- the same
                # drop the +3V3 and VBUS pads get. A ground pad whose only
                # connection was a floating sliver was never grounded; this is
                # where that stops being hidden by the sliver.
                for fp in board.GetFootprints():
                    for pad in fp.Pads():
                        if pad.GetNetCode() != code:
                            continue
                        if not island.Collide(pad.GetPosition()):
                            continue
                        sources = {(i, j, lay) for lay in ROUTE_LAYERS
                                   for (i, j) in pad_cells(grid, pad)
                                   if pad.IsOnLayer(lay)}
                        path = astar(grid, code, sources, None,
                                     goal_is_via=True)
                        if path is None:
                            added.append("%s.%s -- no via site now; it gets "
                                         "one before the signals next attempt"
                                         % (fp.GetReference(),
                                            pad.GetPadName()))
                            stranded.add(pad.m_Uuid.AsString())
                            continue
                        path_to_copper(board, grid, path, net, [], vias)
                        add_via(board, grid, path[-1][0], path[-1][1], net,
                                vias)
                        added.append("%s.%s -> In1.Cu, rescued from the "
                                     "sliver it sat in"
                                     % (fp.GetReference(), pad.GetPadName()))
                drop.append((zone, layer, n))
                added.append("%s sliver at %.2f,%.2f -- deleted, nothing it "
                             "could reach" % (board.GetLayerName(layer),
                                              to_mm(box.GetCenter().x),
                                              to_mm(box.GetCenter().y)))
                continue
            if spot is None:
                # Too small to take a via, so it goes. The zones are set to
                # remove unconnected islands, but the scripted ZONE_FILLER
                # does not act on that -- it needs connectivity data it never
                # builds -- so the fill has to be edited directly.
                continue    # no room now; the delete pass will take it
            via = add_via(board, grid, spot[0], spot[1], net, vias)
            anchors.append((via.GetPosition().x, via.GetPosition().y))
            added.append("%s island at %.2f,%.2f -- via added"
                         % (board.GetLayerName(layer),
                            to_mm(box.GetCenter().x), to_mm(box.GetCenter().y)))

    grouped = {}
    for zone, layer, n in drop:
        grouped.setdefault((id(zone), layer), [zone, layer, []])[2].append(n)
    for zone, layer, outlines in grouped.values():
        filled = zone.GetFilledPolysList(layer)
        kept = pcbnew.SHAPE_POLY_SET()
        for n in range(filled.OutlineCount()):
            if n not in outlines:
                kept.AddOutline(filled.Outline(n))
                for h in range(filled.HoleCount(n)):
                    kept.AddHole(filled.Hole(n, h), kept.OutlineCount() - 1)
        zone.SetFilledPolysList(layer, kept)
    return added, stranded


def escape_room(grid, pad, radius_mm=1.2):
    """How many cells around a pad a track could actually leave through.

    A pad in the middle of a 0.5mm-pitch row has almost none; a 0402 in open
    board has plenty. This is the number that decides whether a net is hard,
    not how many pads it has.
    """
    code = pad.GetNetCode()
    pos = pad.GetPosition()
    ci, cj = grid.ix(pos.x), grid.iy(pos.y)
    r = int(mm(radius_mm) / grid.step)
    free = 0
    for dj in range(-r, r + 1):
        for di in range(-r, r + 1):
            i, j = ci + di, cj + dj
            if not grid.inside(i, j):
                continue
            for layer in ROUTE_LAYERS:
                if not pad.IsOnLayer(layer):
                    continue
                if grid.t[layer][grid.cell(i, j)] in (0, code):
                    free += 1
    return free


def signal_order(nets, promoted=None, grid=None, cover=None):
    """Nets worth routing, hardest and most critical first.

    Order decides how much of the board a net gets to choose from, so it is
    the main thing that makes routing succeed or fail:

      1. anything previous attempts could not route, the most persistent
         failures first
      2. the nets whose geometry matters -- the pair, then the charge pump
      3. the rest by how boxed-in their tightest pad is. A net landing in the
         middle of the module's pad row has one lane out and loses it to
         whoever routes past first; a net between two 0402s in open board can
         wait. Pad count breaks the tie -- eleven terminals have less freedom
         than two -- and then span, longest first: a net crossing the whole
         board has to find a corridor through everything, and going last
         means there is none left

    Everything on a plane is skipped, and so is anything with one pad -- a
    net with a single terminal has nothing to connect.
    """
    promoted = promoted or {}
    todo = []
    for name, pads in nets.items():
        covered = cover.get(name, (None, set()))[1] if cover else set()
        loose = [(fp, p) for fp, p in pads
                 if p.m_Uuid.AsString() not in covered]
        # A net whose pour reaches all of it needs no routing. One with pads
        # outside the pour still does -- for those the pour is a destination,
        # not an answer.
        if not loose or len(pads) < 2:
            continue
        xs = [p.GetPosition().x for _, p in pads]
        ys = [p.GetPosition().y for _, p in pads]
        span = (max(xs) - min(xs)) + (max(ys) - min(ys))
        if name in promoted:
            rank = (0, -promoted[name])
        elif name in PRIORITY_NETS:
            rank = (1, PRIORITY_NETS.index(name))
        else:
            rank = (2, 0)
        tight = (min(escape_room(grid, p) for _, p in pads)
                 if grid is not None else 0)
        todo.append((rank, tight, -len(pads), -span, name, pads))
    todo.sort(key=lambda r: (r[0], r[1], r[2], r[3], r[4]))
    return [(name, pads) for _, _, _, _, name, pads in todo]


def route_net(board, grid, name, pads, tracks, vias, base=None,
              crossed=None, covered=frozenset()):
    """Connect every pad of one net, nearest pad first.

    Grows one connected island. Each hop takes the geometrically nearest
    unconnected pad and routes to it, falling back to the next nearest if
    that one is walled in.

    Searching to every remaining pad and taking the cheapest gives slightly
    tidier copper, but it is one search per remaining pad per hop, and a
    failed search is the expensive kind -- an eleven-pad net costs fifty-five
    of them, most of them hopeless. Nearest-first is one search per hop.
    """
    net = pads[0][1].GetNet()
    code = net.GetNetCode()
    # Pads a pour already ties together start out as one island: the copper
    # between them exists, so the only work left is reaching it. Without this
    # the router would lay tracks alongside its own pour.
    seed, remaining = [], []
    for fp, pad in pads:
        (seed if pad.m_Uuid.AsString() in covered
         else remaining).append((fp, pad))
    if not seed:
        seed, remaining = remaining[:1], remaining[1:]
    island = {(i, j, layer) for _, pad in seed for layer in ROUTE_LAYERS
              for (i, j) in pad_cells(grid, pad) if pad.IsOnLayer(layer)}
    anchors = [centre(pad) for _, pad in seed]
    hops = 0
    stuck = []
    while remaining:
        remaining.sort(key=lambda fp_pad: nearest(centre(fp_pad[1]), anchors))
        joined = False
        for k, (fp, pad) in enumerate(remaining):
            targets = {(i, j, layer) for layer in ROUTE_LAYERS
                       for (i, j) in pad_cells(grid, pad)
                       if pad.IsOnLayer(layer)}
            if targets & island:
                island.update(targets)
                anchors.append(centre(pad))
                remaining.pop(k)
                joined = True
                break
            path = astar(grid, code, island, targets, base=base,
                         crossed=crossed,
                         limit=SCOUT_EXPANSIONS if base is not None else None)
            if path is None:
                continue
            if base is not None:
                # Only scouting for blockers -- do not lay copper through
                # someone else's track.
                island.update(path)
                island.update(targets)
                anchors.append(centre(pad))
                remaining.pop(k)
                joined = True
                break
            path_to_copper(board, grid, path, net, tracks, vias)
            island.update(path)
            island.update(targets)
            anchors.append(centre(pad))
            remaining.pop(k)
            hops += 1
            joined = True
            break
        if not joined:
            stuck = remaining
            break
    if stuck:
        left = ", ".join("%s.%s" % (fp.GetReference(), pad.GetPadName())
                         for fp, pad in stuck)
        return False, "%-11s %d of %d pad(s) unreachable: %s" % (
            name, len(stuck), len(pads), left)
    return True, "%-11s %d pad(s), %d hop(s)" % (name, len(pads), hops)


def route_one(board, grid, name, pads, owned, detail, covered=frozenset()):
    """Route one net, remembering its copper so it can be ripped up again."""
    mine_t, mine_v = [], []
    ok, line = route_net(board, grid, name, pads, mine_t, mine_v,
                         covered=covered)
    owned[name] = mine_t + mine_v
    detail[name] = line
    return ok


def stamp_item(grid, item):
    """Put an existing track or via back into the occupancy grids."""
    code = item.GetNetCode()
    if isinstance(item, pcbnew.PCB_VIA):
        p = item.GetPosition()
        for layer in ROUTE_LAYERS:
            grid.stamp_disc(grid.t[layer], p.x, p.y,
                            mm(VIA_D_MM / 2 + CLEAR_MM + TRACK_MM / 2), code,
                            merge=True)
            grid.stamp_disc(grid.v[layer], p.x, p.y,
                            mm(VIA_D_MM / 2 + CLEAR_MM + VIA_D_MM / 2), code,
                            merge=True)
        return
    a, b, layer = item.GetStart(), item.GetEnd(), item.GetLayer()
    grid.stamp_seg(grid.t[layer], a.x, a.y, b.x, b.y,
                   mm(TRACK_MM / 2 + CLEAR_MM + TRACK_MM / 2), code, merge=True)
    grid.stamp_seg(grid.v[layer], a.x, a.y, b.x, b.y,
                   mm(TRACK_MM / 2 + CLEAR_MM + VIA_D_MM / 2), code, merge=True)


def rebuild(grid, snap, board):
    """Reset the grids to `snap` and re-stamp every track and via on the board.

    From the board, not from the router's own record of what it laid. Those
    two disagree the moment anything puts copper down or takes it away without
    updating the record, and then the grid describes a board that does not
    exist: nets route through copper it has forgotten, or refuse corridors
    that are actually clear. Every silent short this router has produced came
    from that gap. The board is the only thing that is definitely true, so it
    is the only thing the grid is derived from.

    `snap` is the board before any copper -- outline, keepouts, pads, holes --
    so this is a complete recomputation, not a patch.

    Stamping is order-independent: a cell goes free -> one net -> contested,
    and never back, so re-stamping the same items always lands in the same
    place. That is what lets `audit` compare an incrementally-updated grid
    against a fresh one and call any difference a bug.
    """
    grid.restore(snap)
    for item in board.GetTracks():
        stamp_item(grid, item)


def audit(grid, snap, board):
    """Check the grid still describes the board. Returns what disagrees.

    The router stamps incrementally as it lays copper, because rebuilding
    after every track would be far too slow. This is the cheap proof that the
    incremental path has not drifted: recompute from the board and compare.
    Any difference is a bug in this file, not a property of the design.
    """
    live = grid.snapshot()
    rebuild(grid, snap, board)
    fresh = grid.snapshot()
    bad = []
    for which, a, b in (("track", live[0], fresh[0]), ("via", live[1], fresh[1])):
        for layer in ROUTE_LAYERS:
            if a[layer] == b[layer]:
                continue
            n = sum(1 for x, y in zip(a[layer], b[layer]) if x != y)
            first = next(i for i, (x, y) in enumerate(zip(a[layer], b[layer]))
                         if x != y)
            bad.append("%s grid on layer %d: %d cell(s) differ, first at "
                       "%.2f,%.2f (grid says %d, the board says %d)"
                       % (which, layer, n,
                          to_mm(grid.x(first % grid.w)),
                          to_mm(grid.y(first // grid.w)),
                          a[layer][first], b[layer][first]))
    return bad


def centre(pad):
    p = pad.GetPosition()
    return (p.x, p.y)


def nearest(point, anchors):
    x, y = point
    return min((x - ax) ** 2 + (y - ay) ** 2 for ax, ay in anchors)


# How many times the whole board is re-routed. Each attempt after the first
# raises the priority of every net that has failed so far, and the best
# attempt is the one kept.
#
# Two, because more does not help. Measured over fourteen attempts the
# natural order won every single time, at 3 unrouted, while the reordered
# ones came back with 6 to 11. Re-ordering moves congestion around rather
# than relieving it; rip-up is what relieves it. The second attempt is kept
# only because it costs one pass to be sure of the first.
MAX_ATTEMPTS = 3


def route_pass(promoted, ground=frozenset()):
    """One complete attempt at the board.

    Returns (board, report, failed, stranded) -- `stranded` being the GND pads
    the pours never reached, to be fed back in as `ground` next time.
    """
    out = []

    def say(line=""):
        out.append(line)

    board = pcbnew.LoadBoard(BOARD)

    # ------------------------------------------------------------- strip
    old_tracks = old_vias = 0
    for item in list(board.GetTracks()):
        if isinstance(item, pcbnew.PCB_VIA):
            old_vias += 1
        else:
            old_tracks += 1
        board.RemoveNative(item)
    for zone in board.Zones():
        zone.UnFill()
    say("== stripped")
    say("   %d track(s) and %d via(s) that were on the board already"
        % (old_tracks, old_vias))
    say("   %d zone(s) unfilled" % len(list(board.Zones())))
    say()

    grid = build_grid(board)
    # The board with no copper on it: outline, keepouts, pads, holes. Every
    # later rebuild starts from here and re-stamps the board's own tracks and
    # vias, so the grid can always be recomputed from scratch rather than
    # patched. Taken before the power stage, not after -- power copper is on
    # the board like everything else and gets stamped back with the rest.
    blank = grid.snapshot()
    say("== grid")
    say("   %d x %d cells at %.2fmm, on %d routable layer(s)"
        % (grid.w, grid.h, GRID_MM, len(ROUTE_LAYERS)))
    say()

    tracks, vias = [], []
    nets = collect_nets(board)
    cover = plane_cover(board)
    say("== pours")
    for name in sorted(cover):
        layer, pads = cover[name]
        total = len(nets.get(name, []))
        say("   %-6s on %-7s reaches %d of %d pad(s)%s"
            % (name, layer, len(pads), total,
               "" if len(pads) == total else "  -- the rest are routed"))
    say()

    # ------------------------------------------------------- thermal vias
    thermal = []
    for ref, name, pad in plane_pads(board, cover, big_only=True):
        pos = pad.GetPosition()
        add_via(board, grid, grid.ix(pos.x), grid.iy(pos.y), pad.GetNet(), vias)
        thermal.append("%s.%s -> %s" % (ref, name, cover[name][0]))
    render(say, "vias dropped in the pads no pour can reach", thermal)

    # ------------------------------------------------------- plane drops
    drops, orphans = [], []
    for ref, name, pad in plane_pads(board, cover, big_only=False):
        if name == "GND" and pad.m_Uuid.AsString() not in ground:
            # Most GND pads are reached by the F.Cu and B.Cu pours and need
            # nothing. The boxed-in ones are not -- the pour arrives as a
            # sliver pinched off between tracks, or not at all -- and by the
            # time the signals are down there is nowhere left to put a via.
            # J1.8, J1.17, U3.2, C11.1, R21.2 and R13.2 all ended up with no
            # ground that way, an ESD array's return and a bulk cap's among
            # them.
            #
            # Which pads those are cannot be worked out before filling, and
            # escape room does not predict it -- three of those six had plenty.
            # So the previous attempt's fill says: `ground` holds the pads it
            # left stranded, and they get their via here, while there is still
            # room for one. Same feedback the unroutable nets already use.
            continue
        sources = {(i, j, layer) for layer in ROUTE_LAYERS
                   for (i, j) in pad_cells(grid, pad) if pad.IsOnLayer(layer)}
        path = astar(grid, pad.GetNetCode(), sources, None, goal_is_via=True)
        if path is None:
            orphans.append("%s.%s -- no via site reachable"
                           % (ref, pad.GetPadName()))
            continue
        path_to_copper(board, grid, path, pad.GetNet(), tracks, vias)
        add_via(board, grid, path[-1][0], path[-1][1], pad.GetNet(), vias)
        drops.append("%-4s.%-3s -> %s in %.2fmm"
                     % (ref, pad.GetPadName(), cover[name][0],
                        to_mm(grid.step) * (len(path) - 1)))
    render(say, "pads dropped to their plane", drops)
    render(say, "PLANE PADS WITH NOWHERE TO PUT A VIA", orphans)

    # ------------------------------------------------------------ signals
    # Everything laid so far is power, and none of it is ever ripped up.
    power = tracks + vias

    # Two baselines, and they are not interchangeable:
    #
    #   `blank`     the board with no copper. rebuild() and audit() start here,
    #               because a full recomputation has to begin from something
    #               that owes nothing to what the router thinks it did.
    #   `immovable` blank plus the power copper. This is what the rip-up scout
    #               treats as hard: a cell blocked in `immovable` is a pad, a
    #               keepout or a plane drop and cannot be moved, while a cell
    #               blocked only in the live grid is some signal net's track
    #               and can be ripped. Hand it `blank` instead and the scout
    #               plans routes straight through the power it may not touch.
    immovable = grid.snapshot()

    # `owned` is an index -- which items belong to which net, so a net's
    # copper can be found and removed when it is ripped up. It is deliberately
    # *not* what the grid is built from: see rebuild().
    owned, detail = {}, {}
    order = signal_order(nets, promoted, grid, cover)
    by_name = dict(order)
    # What each net's own pour already ties together, so a net that is part
    # poured and part routed only routes the part that is left.
    tied = {name: cover.get(name, (None, frozenset()))[1] for name, _ in order}
    failed_names = []
    for name, pads in order:
        mine = route_one(board, grid, name, pads, owned, detail, tied[name])
        if not mine:
            failed_names.append(name)

    # -------------------------------------------------------------- rip-up
    # A net that cannot get through is usually not walled in; it has lost a
    # lane to a net that went first. Find which nets are in the way, take
    # their copper out, put this one through the gap and re-route them. This
    # is what takes the board from "almost" to finished.
    ripped_log = []
    for _ in range(RIPUP_ROUNDS):
        if not failed_names:
            break
        for name in list(failed_names):
            if name not in failed_names:
                continue
            crossed = set()
            route_net(board, grid, name, by_name[name], [], [],
                      base=immovable, crossed=crossed, covered=tied[name])
            # Never the priority nets. Routing them first is only worth
            # anything if they keep the route they got.
            blockers = sorted(n for n, items in owned.items()
                              if n != name and items
                              and n not in PRIORITY_NETS
                              and items[0].GetNetCode() in crossed)
            if not blockers:
                continue
            redo = [name] + blockers
            # Every net about to be re-routed gives up its copper first --
            # including `name`, whose failed attempt left a partial path on
            # the board. Taking only the blockers' copper and then calling
            # route_one on `name` replaced owned[name] with the new path and
            # orphaned the old one: still on the board, no longer in `owned`,
            # so the next rebuild() did not stamp it and later nets routed
            # straight through it. That is where SCL crossed EPD_DC and
            # EPD_BUSY on B.Cu -- eight shorts from copper the grid had
            # forgotten was there.
            for n in redo:
                for item in owned.pop(n, []):
                    board.RemoveNative(item)
                detail.pop(n, None)
            rebuild(grid, blank, board)

            still = [n for n in redo
                     if not route_one(board, grid, n, by_name[n], owned,
                                      detail, tied[n])]
            ripped_log.append(
                "%-11s ripped %-38s %s"
                % (name, ", ".join(blockers)[:38],
                   "through" if name not in still else "still stuck"))
            failed_names = [n for n in failed_names if n not in redo] + still

    tracks[:] = [i for i in power if not isinstance(i, pcbnew.PCB_VIA)]
    vias[:] = [i for i in power if isinstance(i, pcbnew.PCB_VIA)]
    for items in owned.values():
        for i in items:
            (vias if isinstance(i, pcbnew.PCB_VIA) else tracks).append(i)

    # The grid has been stamped incrementally and rebuilt several times by
    # now. Check it still describes the board, and leave it freshly rebuilt
    # either way -- the ground stitching below trusts it completely, and a
    # stale cell there means a via dropped on top of a signal via.
    #
    # Anything reported here is a bug in this file. There is no board that can
    # legitimately make the grid and the copper disagree.
    drift = audit(grid, blank, board)
    render(say, "GRID DISAGREED WITH THE BOARD -- this is a bug in route.py",
           drift, empty="no -- the grid describes the board exactly")

    render(say, "rip-up rounds", ripped_log)
    render(say, "routed", [detail[n] for n in sorted(detail)
                           if n not in failed_names])
    render(say, "NOT ROUTED", [detail[n] for n in sorted(failed_names)])

    # ---------------------------------------------------------- stitching
    # Last, in the room the signals left. Stitching first costs signal nets
    # corridors they cannot spare, to buy ground connections the pours would
    # have made anyway.
    stitches = stitch_ground(board, grid, vias)
    say("== ground stitching (%d)" % stitches)
    say("   %d via(s) tying F.Cu, In1.Cu and B.Cu together on a %.1fmm grid"
        % (stitches, STITCH_PITCH_MM))
    say()

    # -------------------------------------------------------------- fill
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    # Vias first, then re-fill so the pours clear around the new copper --
    # a via dropped into an already-filled board sits at zero clearance to
    # the In2.Cu pours it passes through. Only then delete what is still
    # isolated, because the re-fill would put any deleted sliver straight
    # back.
    islands, stranded = stitch_islands(board, grid, vias, delete=False)
    if islands:
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        board.BuildConnectivity()
    more, stranded2 = stitch_islands(board, grid, vias, delete=True)
    islands += more
    stranded |= stranded2
    render(say, "isolated pour islands", islands)

    board.BuildConnectivity()
    unconnected = board.GetConnectivity().GetUnconnectedCount(True)

    say("== result")
    say("   %d track(s), %d via(s), %.1fmm of copper"
        % (len(tracks), len(vias), sum(to_mm(t.GetLength()) for t in tracks)))
    say("   %d unconnected item(s) after filling the zones" % unconnected)
    say()
    return board, "\n".join(out), failed_names + orphans, stranded


def render(say, title, items, empty="none"):
    say("== %s (%d)" % (title, len(items)))
    if not items:
        say("   %s" % empty)
    for i in items:
        say("   %s" % i)
    say()


def main(argv):
    dry_run = "--dry-run" in argv

    pressure, ground, best = {}, set(), None
    for attempt in range(MAX_ATTEMPTS):
        board, report, failed, stranded = route_pass(pressure, ground)
        # Rank on unrouted nets first, then on ground pads the pours missed.
        # A stranded ground pad is a real defect, not a cosmetic one, so a
        # board that grounds everything wins ties.
        score = (len(failed), len(stranded))
        if best is None or score < best[2]:
            best = (board, report, score, failed, attempt + 1, stranded)
        print("attempt %2d: %2d unrouted%s%s"
              % (attempt + 1, len(failed),
                 "" if not failed else "  " + ", ".join(failed),
                 "" if not stranded else
                 "   [%d ground pad(s) stranded]" % len(stranded)))
        if not failed and not stranded:
            break
        for name in failed:
            pressure[name] = pressure.get(name, 0) + 1
        ground |= stranded

    board, report, score, failed, attempt, stranded = best
    print()
    print(report)
    print("best of %d attempt(s) was attempt %d, with %d net(s) unrouted "
          "and %d ground pad(s) stranded"
          % (MAX_ATTEMPTS, attempt, score[0], score[1]))

    if dry_run:
        print("\ndry run, board not written")
        return 0
    board.Save(BOARD)
    print("\nwrote %s" % BOARD)
    return 1 if (failed or stranded) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
