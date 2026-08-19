# Watchy Ref — ESP32-C6-MINI-1 / RV-3028-C7 board

A Watchy-compatible watch board carrying an ESP32-C6-MINI-1 module and an
RV-3028-C7 real-time clock, with the accelerometer, the USB-serial bridge and
the UART console deleted and USB-C in place of micro-USB. Derived from
[sqfmi/watchy-hardware](https://github.com/sqfmi/watchy-hardware) `v2.0`
(MIT), which supplies the e-paper, charging, power and motor circuitry
unchanged.

Design rationale, with the reasoning behind every decision:
[`docs/superpowers/specs/2026-08-18-c6-mini-rv3028-board.md`](../docs/superpowers/specs/2026-08-18-c6-mini-rv3028-board.md).

> **Never fabricated, never powered, never tested.** Read
> [`UNVERIFIED.md`](UNVERIFIED.md) before doing anything with these files.

## The design is code

The electrical design is not a drawn schematic. It is
[atopile](https://atopile.io) source under `elec/src/`, compiled to a KiCad
netlist. Reading `elec/src/watchy.ato` top to bottom tells you what is
connected to what, and `git diff` on it says exactly what a change did —
which is not true of a `.kicad_sch`.

The tradeoff is real and worth stating: there is no schematic sheet to print
and hand to someone. `ato view` draws a block diagram, but it is not a
schematic. If you want one, KiCad's netlist import gives you a rat's nest, not
a drawing.

```
board-files/
  ato.yaml                          project and build config
  requirements.txt                  the compiler version, pinned exactly
  elec/src/watchy.ato               the board: every part and every connection
  elec/src/parts.ato                passives, discretes, connectors, switches
  elec/src/mcu.ato                  ESP32-C6-MINI-1
  elec/src/rtc.ato                  RV-3028-C7
  elec/src/usb.ato                  USB-C receptacle and the ESD array
  elec/footprints/footprints.pretty custom footprints
  elec/layout/default/              the PCB (see "Layout" below)
  checks/netlist_check.py           design assertions against the netlist
  checks/drc_check.py               runs KiCad's DRC and judges the result
  checks/make_bom.py                fab BOM generator
  checks/run.sh                     build, assert, generate, check the board
  elec/layout/v2-baseline.kicad_pcb V2's layout, untouched; the overlap reference
  scripts/apply_netlist.py          netlist -> PCB, headless (mutates the board)
  scripts/route.py                  routes the board, headless (mutates it too)
  scripts/check_board.py            asserts the PCB matches the netlist
  fab/BOM.csv                       generated, committed
  vendor/                           upstream licence notices
```

## Building and checking it

```bash
python3 -m venv ~/.venvs/ato
~/.venvs/ato/bin/pip install -r requirements.txt
ATO=~/.venvs/ato/bin/ato ./checks/run.sh
```

That compiles the sources to `build/default.net`, runs the assertions, writes
`fab/BOM.csv`, checks the board against the netlist and runs DRC. Expect
`0 problem(s)` and 54 placements.

**Install from `requirements.txt`, not `pip install atopile`.** The pin is
`atopile==0.2.67` and it is exact on purpose: the floating install now gets
0.12.6, which refuses to run at all ("the CLI has been replaced by the app at
app.atopile.io"). `checks/run.sh` checks the version and stops if it is
wrong. `requirements.txt` records what every newer release does — the short
version is that 0.3 through 0.11 reject these sources, 0.12 is dead, 0.14 and
0.15 need Python 3.14, and 0.16+ is a hosted application rather than a CLI.
There is no upgrade that keeps this a headless build.

The assertions encode the constraints the spec cares about and that no
compiler can see:

| Check | What it catches |
| --- | --- |
| pin map | any GPIO that drifted from the spec's table |
| deep-sleep wake pins | a button or `RTC_INT` on a pin above IO21, which cannot wake `ext1` |
| battery ADC on ADC1 | the battery tap landing on ADC2, which is dead while WiFi is on |
| strapping pins | anything but the GPIO0 test pad touching IO0/IO3/IO45/IO46 |
| USB-C CC pulldowns | a missing or shared 5.1k, and D+/D- halves left untied |
| RV-3028-C7 wiring | `VDD` off `+BATT`, or `VBACKUP` fed from a rail |
| bus pull-ups | I2C or `RTC_INT` pulled to `+BATT`, putting 4.2V on C6 inputs |
| buttons active low | a switch still wired to `+3V3`, or a leftover pull-down |
| battery divider | the divider back at 100k, or missing its filter cap |
| MCU power | the `-N4R2` variant, a floating module ground pad, EN misrouted |
| V2 blocks carried over | any drift across the 106 pads of the 43 parts transcribed from V2 |
| deleted parts | the PICO-D4, CP2102, BMA423, PCF8563, crystal or antenna creeping back |

And on the board (`scripts/check_board.py`):

| Check | What it catches |
| --- | --- |
| components | a netlist part with no footprint, or a footprint the netlist does not name |
| nets | any pad on the wrong net, or on a net where the netlist says nothing |
| mechanical | the four switches, the FPC and the battery connector drifting off V2's coordinates |
| stackup | the board falling back to 2 layers, or a missing GND / +3V3 plane |
| outline | Edge.Cuts not closing, or a cutout reappearing inside the board |
| pads on board | any pad off the edge — this is how the USB-C was found facing the wrong way |
| antenna | the module's keepout allowing pour, or not overhanging the board edge |

And DRC itself (`checks/drc_check.py`), which runs `kicad-cli pcb drc` and
holds the result to a policy rather than to a count: zero unconnected items,
zero errors except an allowlist that names each accepted violation and says
why. It also reports allowlist entries that no longer occur, so the list
cannot quietly rot.

All of these were mutation-tested: the netlist or the board was deliberately
broken in the way each check describes, and the check caught it. A check that
cannot fail is not a check.

**What is not checked: ERC — and it cannot be.** `kicad-cli sch erc` reads a
`.kicad_sch`, and this project has none; the design is atopile source
compiled straight to a netlist. Nothing looks for driven-output conflicts or
unconnected pins beyond what the assertions above happen to cover. That is a
standing cost of defining the design in code, not a temporary gap. See
[`UNVERIFIED.md`](UNVERIFIED.md).

## Layout

`elec/layout/default/watchy-ref-c6.kicad_pcb` started as V2's board, converted
from KiCad 5 and switched to a 4-layer stackup. Two scripts turn it into this
design, in order, and both are re-runnable:

```bash
python3 scripts/apply_netlist.py --dry-run   # report only
python3 scripts/apply_netlist.py             # placement, nets, planes, outline
python3 scripts/route.py                     # copper
python3 scripts/check_board.py               # verify, independently
```

They need the interpreter KiCad's `pcbnew` bindings were built against — the
system `python3`, not a venv.

### apply_netlist.py — everything except the copper

KiCad's "Import Netlist" is a GUI dialog and `kicad-cli` has no equivalent, so
this does the job with `pcbnew`. It removes the interior cutouts from
Edge.Cuts and the rule areas V2 no longer needs, deletes the parts the netlist
dropped, replaces the ones whose footprint changed *while preserving V2's
position* (most of those swaps are only a KiCad 5→7 library rename, and losing
placement over a rename would defeat the point of pinning the designators),
adds the new parts, assigns all 249 pads to their 73 nets, and lays the GND,
`+3V3` and `VBUS` pour outlines.

It also strips V2's branding — the `watchy` and `SQFMI` lettering on the mask
layers, and the two sqfmi logo footprints. This is a derivative reference
design and should not be fabricated carrying another project's marks; the
attribution belongs in this file, not etched into the board. It happens to be
worth 119 DRC errors as well: mask lettering is drawn as *apertures*, so the
word is bare copper, and every via, track and pad passing beneath one bridged
to it. The display outline (`REF**` on B.Fab) is kept — it comes from the same
library and carries the name, but it is a mechanical outline rather than a
mark, and it is what stops anything being placed under the panel.

Placement comes from a table in the script, and that table is authoritative —
it is re-applied on every run, to parts already on the board as well as to new
ones. The board is generated from these sources, so a position that only
exists because somebody dragged it in the GUI is a position nothing records.
Move a part by editing the table; the script reports every courtyard overlap
the table produces, and the fix is to edit the table again.

### route.py — the copper

There is no autorouter in KiCad and no routing API, so this is a maze router
written against `pcbnew`. It deletes every existing track and via first —
V2's routing was still on the board, and routed for V2's netlist it shorted
nets rather than connecting them — then:

- drops a via into each of the module's thermal pads. It has to: the array is
  1.2mm pads on a 1.65mm pitch, and the 0.45mm gaps are too narrow for a pour
  to enter and too narrow for a via to sit beside. Espressif's reference
  layout does the same.
- ties every pad on a poured net to its pour through its own via, except
  where the pour already touches the pad. Which nets are poured, and which of
  their pads the pour reaches, are read off the board rather than listed in
  the router -- so the two scripts cannot drift apart, and a pour that covers
  only part of its net leaves the rest as ordinary routing work.
- routes the signal nets with A* on F.Cu and B.Cu over a 0.1mm grid, hardest
  first: the differential pair and the charge-pump loop before anything can
  cross them, then whichever nets have the least room to escape their pads.
- rips up and re-routes. A net that cannot get through has usually lost a lane
  to one that went first, so a second search finds which nets are in the way,
  deletes their copper, puts the stuck net through the gap and re-routes them
  somewhere else. This is what takes the board from "almost" to finished.
- stitches GND between F.Cu, In1.Cu and B.Cu on a 3mm grid, last, in whatever
  room the signals left.
- fills the zones and reports what is still unconnected.

Power is mostly not routed as copper. GND and `+3V3` are whole-board planes
on In1.Cu and In2.Cu and a pad reaches them through a via, which is what buys
the space to route everything else on two layers.

`VBUS` is a third pour, and a bounded one. It carries the charge current -- in
at the USB-C, out through the Schottky to VSYS and into the TP4054 -- so it
wants copper rather than a track, and as eleven thin tracks fighting for lanes
it was the net the router most often failed to place. It sits on In2.Cu at a
higher priority than `+3V3`, over the block that actually carries the current:
`J2`'s four VBUS pads, `U3.5`, `C11.2`, `R14.1`, `Q4.1` and `U5.4`.

It is sized to that block and not to the net. A hull around all eleven VBUS
pads is a 202mm2 diagonal band that reaches clear across the board to collect
`D4` and `D5`, and swallows `U2.5` -- the LDO 3V3 output -- on the way, which
would strand it. Bounded to the block it costs 82mm2, traps nothing, and
leaves `D4` and `D5` as two ordinary tracks, which is what they are: stubs
hanging off the block.

In2.Cu rather than B.Cu, which is where an earlier note here suggested putting
it. Both work, but In2.Cu is already a power layer so the pour costs no
routing space, whereas the same shape on B.Cu would sit straight on top of the
`J2`/`U3`/`U5`/`Q4` congestion -- the most contested copper on the board. The
cost is a split in the `+3V3` plane under one corner of B.Cu, a return-path
discontinuity for anything crossing it, which at SPI and 12Mbps USB is not a
real concern. `VBUS_POUR_LAYER` in `apply_netlist.py` moves it.

The pour is not free. The nine covered pads are all on F.Cu, so each needs a
through via to reach In2.Cu, and a through via blocks all four layers -- nine
of them land in the tightest part of the board. `VBUS` itself routes now, and
one fewer net fails overall, but those vias took lanes `USB_DM` and `VSYS` had
been using. It is the right shape for the current path either way; it is not
the thing that finishes the board.

Every via the router places is tented on both sides. Half of them sit in a pad
field or inside a thermal pad, where a bare mask aperture would bridge to the
neighbouring pad and wick solder off the joint.

#### The board is the only source of truth

The router keeps an occupancy grid to search over, and the grid has to agree
with the copper. It used not to. There were three stores of truth rather than
two: the board, the grid, and `owned` -- a side table of which items belong to
which net -- and the grid was rebuilt from `owned`, never from the board. Any
copper that fell out of that table, or was added without updating it, was
still physically on the board and completely invisible to the router.

That is not a hypothetical. It is where `SCL` came to cross `EPD_DC` and
`EPD_BUSY` on B.Cu: a failed attempt left a partial path behind, `owned` was
overwritten with the retry, and every later rebuild pretended the old copper
was not there. Eight shorts, and nothing in the router could have noticed.

So `rebuild()` now stamps from `board.GetTracks()` and the grid is a pure
function of the board. `owned` survives as an index -- it is how a net's items
are found when it is ripped up -- but nothing is derived from it. A net can
drop out of it and the grid stays correct.

That needed one snapshot split into two, because they were doing different
jobs under one name:

| | |
| --- | --- |
| `blank` | the board with **no** copper: outline, keepouts, pads, holes. `rebuild()` and `audit()` start here, so a recomputation owes nothing to the router's own record of itself |
| `immovable` | `blank` plus the power copper. What the rip-up scout treats as *hard*: a cell blocked here is a pad or a plane drop and cannot move, while one blocked only in the live grid is some signal's track and can be ripped |

Handing the scout `blank` would let it plan routes straight through power it
may not touch, which is the bug you get for free by moving the snapshot
without noticing it had two roles.

Searching still stamps incrementally -- rebuilding after every track would be
far too slow -- so correctness rests on the incremental path matching a fresh
one. `audit()` proves it: recompute from the board, compare cell by cell,
report any difference with its coordinates and both values. This is sound
because stamping is monotone: a cell goes free -> one net -> contested and
never back, so re-stamping the same items always lands in the same state
whatever order they arrive in. A fresh rebuild is therefore the ground truth
an incrementally-updated grid must equal.

It runs every pass and prints under "GRID DISAGREED WITH THE BOARD". Anything
it reports is a bug in `route.py`; no board can legitimately make the grid and
the copper disagree. It is mutation-tested in both directions -- copper added
to the board without telling the grid, and copper removed from it -- and
catches each with the cell coordinates and both values.

#### What it does not finish

The router does not get everything. It routes the power, every ground pad and
41 of the 44 signal nets; **three are left unrouted** — `EPD_SCK` at `U1.28`,
`PREVGH` at `J1.21` and `USB_DM` at `J2.A7` — for 3 unconnected items on the
board. `route.py` exits non-zero, and `drc_check.py` and `checks/run.sh` fail,
until they are cleared. That is deliberate: a board that is 93% routed should
not be able to look finished.

`route.py` also exits non-zero on a stranded ground pad — one the pours never
reached and no via was dropped into — because a floating return is a defect
and not a cosmetic complaint. There are none at present, but they are the
failure this board produces when the right-hand side gets crowded, and they
were invisible until the pour slivers hiding them were deleted.

Which nets and which pads move between runs — the router is not deterministic
across attempts, because each attempt feeds the previous one's failures back
in. The counts are stable; the names are not.

What the router does lay down is clean. On the current board it produces **no
shorts, no crossing tracks and no clearance violations of its own**, and DRC
reports **zero errors that are not on `drc_check.py`'s accepted list** — every
clearance and hole-clearance error left is internal to a footprint (`Q4`/`R15`
at 0.165mm, inherited from V2; `J2`'s own mounting holes against its own
pads). Getting there took four fixes, each found by DRC rather than by design,
and they are worth knowing before trusting the next change:

| Bug | Symptom |
| --- | --- |
| overlapping clearance halos resolved last-writer-wins | a cell claimed by two nets became one net's, and that net then routed through the other's clearance. 96 clearance errors |
| diagonal steps cut corners | a 45° step passes between two cells it never occupies, so two opposite diagonals cross without ever sharing one. KiCad calls that a short |
| the grid went stale after rip-up | ground stitching dropped vias on top of signal vias, 0.4mm apart |
| pads keyed by (ref, name) | `J2` has four separate shield pads all called `S1`, and covering one counted as covering all four |
| pads were treated as their bounding boxes | the test points are 1mm circles and the motor's pad is an oval, so the bbox corners are outside the copper. A track that ended in one stopped short of the pad it had been routed to — and the router counted the net done, because its own reachability test used the same bbox. Six nets were "routed" and not connected |
| fill islands with nothing to reach | a pour pinched off between two tracks is still copper, still floating, and counted as unconnected. `route.py` now vias any island big enough to take one, and `apply_netlist.py` sets the zones to delete the rest |
| drilled holes were invisible to the router | an NPTH pad is a hole with no copper, so it is on no copper layer, so the pad loop skipped it entirely. Tracks routed straight over `J2`'s mounting holes and the four switches' |
| the design rules reverted behind the router's back | the rules live in the `.kicad_pro`, and loading a second board anywhere in `apply_netlist.py` makes KiCad's settings manager flush a stale copy of the project over them. 0.15 went quietly back to 0.2, and a board routed at 0.15 came back with 76 clearance errors against a rule nobody had meant to change. `apply_netlist.py` now writes them last and `check_board.py` asserts them |

Finish them in the GUI, or improve the router. What was already tried, so the
next person does not repeat it:

| Lever | Result |
| --- | --- |
| via cost swept 6 → 80 | 25 is the best; cheap vias cost more board than they save |
| ordering by pad count, by escape room, by span | all help a little; none alone gets below 5 |
| re-ordering by accumulated failure pressure, 14 attempts | the natural order won every time |
| rip-up and re-route | 5 → 3, and the only thing that moved the number much |
| search budget 80k → 250k | fixed searches that were being cut off, not the remainder |
| `VBUS` as a pour instead of a signal net | `VBUS` routes, and one fewer net fails overall — but the nine through vias it needs land in the tightest part of the board, and cost `USB_DM` and `VSYS` their lanes |

Those that remain are not walled in — each routes on its own on an empty
board — so this is congestion, not geometry, and measurably so: the pads that
*failed* have more room around them than the ones that succeeded. `J1`'s
tightest pins have 177 free cells within 1.2mm and all of them routed; the
eight that did not had 201 to 542, against a board median of 383. Nothing
failed for want of space at the pad. They failed for want of a lane, and the
lane went to whichever net asked first.

Five of the seven are at the display connector `J1` or the module pins feeding
it, which is the densest corner of the board. The lever there is not the FPC
pinout — that is fixed by the panel — but the *module* side: `EPD_SCK`,
`EPD_MOSI`, `EPD_CS`, `EPD_DC`, `EPD_RST` and `EPD_BUSY` are assigned to
`IO47`, `IO48`, `IO33`, `IO34`, `IO35` and `IO36` in `elec/src/watchy.ato`,
and those are general-purpose pins. Re-ordering them so the fan-out crosses
itself less is a change to the source, which is where this design's decisions
are supposed to live, and it is the one move the router cannot make for
itself.

Clearance is modelled as the true offset of each pad shape, not its bounding
box grown by the clearance. The difference is only at the corners, and the
corners are the whole game on a 0.5mm-pitch connector: a pad in a row that
close can only be escaped along its own axis, and squaring off the
neighbours' clearance closes exactly that lane. With bounding boxes, six of
the USB-C's pads were unreachable from anywhere.

### Placement

The parts the new layout displaced are placed by anchoring each to the pad it
actually serves and taking the nearest clear spot to it — which is what you
would do by hand, and is what decoupling wants anyway:

| Part | Anchored to | Distance |
| --- | --- | --- |
| `C12` +3V3 bulk | `U1` pad 3 (3V3) | 3.00 mm |
| `C1` EN cap | `U1` pad 45 (EN) | 2.00 mm |
| `R1` EN pull-up | `U1` pad 45 | 3.20 mm |
| `R7` RTC_INT pull-up | `U6` pin 2 (INT) | 2.12 mm |
| `Q5` motor driver | `M1` pad 2 | 3.50 mm |
| `U2` LDO | `Q4` pad 2 | **11.07 mm — review** |
| `D4` OR-ing diode | `Q4` pad 2 | **12.81 mm — review** |

`U2` and `D4` are flagged because the area around `Q4` is dense — the charger,
its programming resistors and the FPC connector are all there — and the
nearest clear space is a long way off. Both carry DC power rather than a
switching node, so the distance is tolerable, but it is worth a look before
you trust the routing there.

### The USB-C, and what it costs

`J2` is rotated 90°, with the front face of its housing flush with the board
edge at x=102.5. This matters more than it sounds: the footprint's solder tabs
are at its local −y end and the mating opening at +y, so at 0° the connector
faces *down the board* and six of its pads — including both shield tabs — hang
over the right-hand edge in mid-air. It was at 0°. `check_board.py` now
asserts every pad is on the board.

The receptacle does not really fit where V2's micro-USB was. There is 9.15mm
between the battery connector and the charger; a USB-C shell is 9.04mm wide
and its courtyard is 10.73mm. So three courtyard overlaps remain and are
accepted on purpose: `J2`/`J3` by 0.61mm, `J2`/`R16` by 0.49mm, `J2`/`Q4` by
0.13mm. The parts themselves clear each other. Moving `J2` anywhere else means
moving a part the case is cut to, or putting the port on an edge that has no
room for a 7.5mm-deep connector.

Everything else V2 carried is left where V2 had it — including a handful of
courtyard overlaps of 0.05–0.25mm that come from KiCad 9's footprints being
fractionally larger than KiCad 5's. Lifting `R8` and `R9` 0.3mm to clear one
of them was tried and reverted: it moved two parts of the charge-pump loop
off the geometry V2 proved, which is a bad trade for a rectangle that does not
touch any copper.

### Board outline

The Edge.Cuts outline measures **33.800 × 45.960 mm** on the centreline
(33.950 × 46.110 mm including the 0.15mm line width). sqfmi's published figure
of 35.5 × 46.0 mm is wrong, or at least does not describe this outline.

**V2's two watch-strap slots have been removed.** They ran at y 72.15–74.15
and y 111.61–114.11, straight through the board, and the module and its
decoupling were placed across the top one — `U1` pads 2, 3, 43 and 44, both
pads of `R1`, both of `C12` and one of `C1` were over a hole. Removing the
slots was chosen over moving the module.

The consequence is mechanical and it belongs to the case: **there is nothing
on this PCB for a strap to pin through any more, and nothing to take the
load.** The case has to carry the entire band attachment. See
[`docs/superpowers/specs/2026-08-18-waterproof-case-design.md`](../docs/superpowers/specs/2026-08-18-waterproof-case-design.md).

Board thickness stays 1.6mm so the case stack is unchanged.

### Why there is a second copy of the board

`elec/layout/v2-baseline.kicad_pcb` is V2's layout converted and given the
4-layer stackup, otherwise untouched. The overlap report diffs against it.
Diffing against the working board instead would be self-excusing: after one
run the working board *is* this script's output, so every overlap it
introduced would come back labelled "V2 already had this one". Do not edit it.

### KiCad 9 notes

- `pcbnew`'s bindings are on the system `python3` (3.13 here), not the
  `python3.12` the older instructions named.
- `board.Remove()` hands ownership of the item to Python, and freeing it there
  corrupts the SWIG type registry — the *next* `LoadBoard` comes back as a
  bare `SwigPyObject` with no methods. Use `RemoveNative()`, which deletes it
  in C++. Both scripts do.
- `PCB_VIA.SetWidth()` needs a layer argument now.
- Opening the project in KiCad 9 rewrites `watchy-ref-c6.kicad_pro` into the
  new format. No design rule changed in that upgrade; `hole_near_hole` was
  renamed `hole_to_hole` and demoted to a warning, and `text_on_edge_cuts` was
  added as an error.

## Fabrication

Nothing here is ready to order — see [`UNVERIFIED.md`](UNVERIFIED.md) — but
when it is:

- 4 layers, 1.6mm, ENIG. The module, the RV-3028 and the 0.5mm FPC connector
  are all fine pitch; HASL is not worth the yield risk.
- Tracks are 0.15mm with 0.15mm clearance, vias 0.6mm with 0.3mm drills. Any
  fab that will build a 4-layer board at all will build 0.15/0.15; it is not
  an "advanced" option and carries no price break to avoid. The board was
  0.2/0.2, inherited from V2, and on a board whose problem is corridors that
  was costing a quarter of every one of them.
- Solder mask expansion is 0.05mm. V2 had 0.2mm, which makes every opening
  0.4mm wider than its pad — on 0.5mm-pitch parts that guarantees neighbouring
  openings merge, and once the tracks came down to 0.15mm it bridged them to
  passing copper too. It was worth 155 DRC violations on its own.
- GND pads connect to the F.Cu and B.Cu pours **solidly**, not through thermal
  spokes. This is a trade: thermal relief exists to stop a pour sinking heat
  away from a small pad during reflow, and solid connections raise the risk of
  a 0402 tombstoning. Against that, GND pads get no via down to the In1.Cu
  plane — the router skips them because the outer pours already touch them —
  so the pour *is* the ground connection, and on twenty of them it could only
  reach with a single 0.15mm spoke. If a fab pushes back, the other fix is to
  keep thermal relief and give every GND pad its own via to In1.Cu, which
  costs routing space this board does not have spare.
- **Vias in pads**, in the module's thermal array only, where nothing else
  reaches. Ask for them plugged and capped, or expect solder to wick off
  those pads into the barrel.
- Single-sided assembly: every part is on F.Cu.

Order `ESP32-C6-MINI-1-N4` (4MB flash) or `-H8` (8MB). There is no PSRAM
variant to get wrong, unlike the S3-MINI-1's `-N4R2`. Specify genuine Micro
Crystal for the RV-3028-C7.

The display, battery, vibration motor and case assembly are hand work. No
assembly house will finish this board into a watch.

## Firmware

Firmware support for this board does not exist. `RefRtc` short-circuits to the
internal clock on any C6 build and has no RV-3028 driver. The
"Firmware impact" section of the spec lists what is needed; it is a separate
plan and nothing in this directory implements any of it.

The panel the firmware drives is now the **GDEY0154D67**, which replaces the
end-of-life GDEH0154D67. Same SSD1681 controller, same 200x200, same 24-pin
FPC, and GxEPD2 takes the same four-pin constructor — but the GDEY's FPC
pinout has not been read off a datasheet and compared against `J1` pin by
pin. Do that before you power a panel.

## Licences

- The V2 design this is derived from: MIT, `vendor/LICENSE-watchy-hardware.txt`.
- The ESP32-C6-MINI-1 footprint: CC-BY-SA 4.0 from
  [espressif/kicad-libraries](https://github.com/espressif/kicad-libraries),
  `vendor/LICENSE-espressif-kicad-libraries.md`.
