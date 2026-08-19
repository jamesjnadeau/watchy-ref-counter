# Waterproof Case for Watchy — Design

Status: approved in principle, pending final spec review
Date: 2026-08-18
Target hardware: Watchy V3 (ESP32-S3), modelled on documented V2 geometry
with a USB-C port substituted for micro-USB.

## Purpose

The ref counter is used outdoors, on a field, in whatever weather the game is
played in. sqfmi lists the stock Watchy's waterproof rating as "No" — it is
open at the buttons and at the port. This design replaces the stock case with
a sealed enclosure that survives rain, sweat and wet hands for the length of a
game, and that can still be opened to charge and service the watch.

## Protection target

**IPX4-IPX5.** Splashing and low-pressure jets from any direction: heavy rain,
a sideline downpour, a wet towel, a rinse under a tap.

**Not IPX7/IPX8.** The case is not rated for submersion and must not be worn
swimming. A submersion rating would require potting the electronics, which
makes the watch unserviceable and unchargeable — an unacceptable trade for a
device whose battery is its consumable.

FDM prints are porous between layers under sustained pressure. The print
settings in the case README mitigate this but do not eliminate it; the rating
above assumes those settings are followed.

## Reference dimensions

The V3 board's mechanical drawings are not published — sqfmi's
`watchy-hardware` repository carries no ESP32-S3 files and `watchy-cases`
ships STEP bodies without a dimension table. Per direction, the model is built
against documented V2 geometry with USB-C assumed in place of micro-USB.

| Quantity | Value | Confidence |
| --- | --- | --- |
| PCB | 33.80 x 45.96mm | **Measured** — see below |
| PCB thickness | 1.6mm | Published |
| Stock assembled watch | 34 x 46 x 9mm, 18g | Published |
| Display | 1.54", 200x200 | Published |
| Battery | 200mAh, 3.7V | Published |
| Battery thickness | ~3.0mm | Derived from the 9mm stack |
| Button positions | Display corners, side-actuated | User-confirmed |
| USB-C opening | 9.0 x 3.2mm | Connector standard |

The 46mm PCB length includes integral strap-lug tabs. Those tabs sit *inside*
the sealed volume — the case provides its own lugs — which is why the case is
appreciably longer than it is wide.

### The width figure is settled, and it was not 35.5mm

The published 35.5 x 46.0mm was never measured; it was taken from sqfmi's
listing. The Edge.Cuts outline of the board this case is for has since been
measured with `pcbnew`: **33.800 x 45.960mm** on the centreline, 33.950 x
46.110mm including the 0.15mm outline width. The assembled watch being listed
at 34mm is the figure consistent with that; the 35.5mm one is not.

Build the pocket against 33.80 x 45.96mm. That is 1.7mm narrower than this
document previously assumed, so a pocket cut to the old figure is loose
rather than tight — foam takes it up, but the lugs and the USB-C opening
shift with it and those do not.

### The buttons are all at V2's positions after all

An earlier revision of this design moved `SW2` -- the BACK button -- 0.2mm up
the board, and this section used to say the case had to follow. It does not.
The move has been reverted and all four buttons sit exactly where V2 has them.

The reason it was needed is worth keeping, because it constrains any future
change in this corner. The USB-C receptacle's shield tabs are through-hole and
stick out past the housing, and the battery connector `J3` had a mounting pad
sitting *on* one -- 0.067mm of overlap, which is a short, not a tight
clearance. `J3` is pinched between that tab and this button, in a corridor of
5.430mm, and `J3` is 5.082mm long. At the 0.2mm copper clearance the board
used to carry it needed 5.482mm and did not fit, so the button gave up the
difference. The board now runs 0.15mm rules, where it needs 5.382mm and fits
with 0.174mm at each end, so `J3` alone carries the fix -- it sits 0.241mm up
the board from V2, and nothing case-facing has moved.

**No dimension in this document changes.** The section is kept because the
corridor is now spoken for to within 0.05mm: anything that widens `J3`, moves
`SW2`, or takes the copper clearance back to 0.2mm puts the shield tab back on
the battery connector.

### Superseded: SW2 moved 0.2mm, and the case must follow

`SW2` -- the BACK button -- sits at (100.57, 79.73) in board coordinates,
0.2mm further up the board than V2 put it.

The reason is copper, not ergonomics. The USB-C receptacle's shield tabs are
through-hole and stick out past its housing, and the receptacle is 9.04mm wide
in a 9.15mm gap. Left where V2 had them, the battery connector `J3` and the
charger `U5` both had a pad sitting *on* a tab -- 0.067mm and 0.120mm of
overlap, which is a short rather than a tight clearance. `U5` moved down 0.45mm
and `J3` up 0.28mm to clear them, but `J3` is pinched between the tab and this
button and the corridor was 0.05mm too narrow to fit it with clearance at both
ends, so the button yielded the difference.

0.2mm is far inside any FDM case tolerance and inside the pusher travel, so no
dimension in this document changes. It is recorded because it is a real
mechanical change to a case-facing part, and because the button positions in
the constraint table above are marked *User-confirmed* -- if those positions
were measured off a physical stock case rather than off the board, this is the
one that no longer matches.

### The board no longer has strap slots

V2 cut two 2 x 28mm slots through the PCB, at y 72.15–74.15 and 111.61–114.11
in board coordinates, for the strap to pin through. This board does not have
them: they ran directly under the module and its decoupling, and they were
removed rather than move the module (`board-files/README.md`, "Board
outline").

**The case must therefore carry the entire strap attachment.** There is no
longer anything on the PCB for a spring bar or a pin to pass through, and
nothing to take the load. The lugs, and whatever anchors them to the sealed
body, are now the only mechanical path between the watch and the band. The
20mm spring-bar lugs in the parts list below already assume case-mounted
lugs, so the design does not change -- but it is no longer a choice, and a
case that relied on the board's slots would have nothing to grip.

Everything above is tagged in `params.scad` as `PUBLISHED`, `DERIVED` or
`ESTIMATED`. The derived and estimated values must be confirmed with calipers
before committing to a full print.

## Architecture

**The case splits horizontally through the button axis.** This is the decision
everything else follows from.

Because the buttons are pressed inward from the side edges, a split at that
height puts every button on the parting line. The consequence is that the
gasket and the button seals become *one part*: a single planar TPU gasket with
a perimeter sealing bead that detours outward into a dome at each of the four
button stations. The seal line stays continuous — bead, bead, dome, bead — and
the domes are simply local excursions of it.

That matters for two reasons. It gives a continuous elastomer boundary with no
joints between separate sealing elements, which is stronger than two seals in
series. And it costs one land in the flange instead of two, which is worth
about 9mm across the watch — the difference between a large watch and an
unwearable one.

Rigid shells were chosen over an all-TPU tub because TPU shrinks
unpredictably and its screw bosses pull through under clamp load. Only the
gasket is soft, and it is the one part whose dimensions are forgiving.

O-ring pushrod buttons were rejected: four sliding seals in FDM bores are four
leak paths, and the dome removes the through-hole entirely.

## Sealing

Four paths water can take in. Each is closed independently.

| Path | Closure |
| --- | --- |
| Parting line | 1.5mm bead on the TPU gasket, in a 1.9 x 1.1mm groove, 27% compression |
| Four buttons | Integral domes on the same gasket; pushers ride outside the boundary |
| USB-C port | Recessed well in the sidewall, tethered TPU bung; well is drained |
| Display window | 1mm clear pane bonded into an internal rabbet with RTV |

### Parting line and buttons

```
   outside                                        inside
      |                                              |
      v                                              v

              pusher (free, unsealed, in split channel)
                    |
                    v
     ==========+   ___   +===================   TOP SHELL
               |  /   \  |
        wall   | ( dome )|   <- TPU gasket, one part
               |  \___/  |
     ==========+         +===================   BOTTOM SHELL
                    ^
                    |
            bead in 1.9 x 1.1 groove
            (the dome is a local outward
             excursion of the same bead line)

   shells bottom out flange-to-flange: compression is
   limited by a hard stop, not by screw torque
```

Groove cross-section is 2.09mm2 against a 1.767mm2 bead, leaving 18% room for
displaced material, so the bead is not extruded out of the groove when the
screws are driven. Compression is (1.5 - 1.1) / 1.5 = 27%, inside the 20-30%
band an elastomer wants for a static face seal. At full torque the two flanges
meet, so the gasket cannot be crushed however hard the screws are tightened.

Each printed pusher rides in a half-round channel split between the two
shells, and presses the dome, which presses the tact switch. The pusher lives
entirely outside the sealed boundary. Water in its channel is stopped by the
dome and drains back out of the channel mouth.

### Port

The USB-C opening sits at the bottom of a recessed well, so water running down
the case does not pool at the connector. The well drains through a slot in its
lower lip. A TPU bung, tethered to the case so it cannot be lost on a field,
closes the well.

### Window

The e-paper module is not itself waterproof — its glass front terminates in an
FPC ribbon. A 1mm clear pane is bonded from the inside into a rabbet in the
top shell with neutral-cure RTV, and the display sits behind it on a
closed-cell foam spacer that holds it against the pane and takes up stack
tolerance. Bonding the pane rather than the display keeps the display
removable.

## Battery

The pocket depth is parametric, with two rendered variants.

| Variant | Pocket | Cell | Case thickness |
| --- | --- | --- | --- |
| `standard` | 3.5mm | Stock 200mAh | ~12mm |
| `extended` | 6.0mm | ~400-500mAh | ~14mm |

Thickness budget, standard variant:

| Layer | mm |
| --- | --- |
| Bottom shell floor | 1.8 |
| Battery pocket | 3.5 |
| PCB | 1.6 |
| Components above PCB to display glass | 2.0 |
| Foam spacer | 0.8 |
| Clear pane | 1.0 |
| Top shell over pane | 1.2 |
| Total | 11.9 |

The extended variant adds 2.5mm to the pocket and nothing else. Since a sealed
case is one you want to open as rarely as possible, roughly doubling runtime
for 2mm of thickness is a good trade — but both are built, and the choice is a
build target rather than an edit.

## Size

| | Stock | This case |
| --- | --- | --- |
| Width | 34mm | ~43mm |
| Length | 46mm | ~53mm |
| Thickness | 9mm | ~12mm / ~14mm |

Per side the flange costs 0.3mm of board clearance plus a 3.4mm sealing land.
Sealing a 35.5 x 46mm board costs roughly 7mm in each in-plane direction and
there is no way around it that does not compromise the seal. This is a large
watch — chunky G-Shock territory. That is a deliberate, stated cost of the
protection target, not an oversight.

## Ergonomics

The four buttons do not have equal roles in this firmware, and the case should
reflect that. From the control map in the repo README:

| Button | Role | Pusher |
| --- | --- | --- |
| Top right | Long clock (40s) | Wide, 1.5mm proud |
| Bottom right | Short clock (25s) | Wide, 1.5mm proud |
| Top left | Sleep | Narrow, flush |
| Bottom left | Menu / clear | Narrow, flush |

The two clock buttons are found and pressed mid-play, possibly with gloves, so
they are large and stand proud of the wall. Sleep and menu are setup-time
controls whose accidental activation during a drive would be costly, so they
sit flush with the wall and need a deliberate fingertip. This asymmetry is the
reason to build a case for this firmware rather than print a generic one.

## Parts

Printed rigid (PETG or ASA):

- `shell_bottom` — tub, battery pocket, PCB standoffs, gasket groove, port well, lugs
- `shell_top` — bezel, window rabbet, gasket land, screw counterbores, lugs
- `pusher` x4 — two wide, two narrow

Printed TPU:

- `gasket` — one part: perimeter bead plus four integral button domes
- `port_bung` — tethered

Bought:

- 6x M2x10 socket head screws
- 6x M2 heat-set inserts, 3.2mm OD, 4.0mm long
- 1mm clear acrylic or PET pane, cut to the rabbet
- Closed-cell foam sheet, ~1mm
- Neutral-cure RTV silicone
- Extended variant only: one 400-500mAh LiPo with a 1.25mm 2-pin connector

Six screws rather than four: the sealing perimeter is about 190mm, and four
screws would leave 48mm spans between clamping points, which is too far to
hold even compression in a printed flange. Six gives roughly 32mm spacing.
The count is parametric.

## Repository layout

```
case/
  params.scad          every dimension, hardware vs design, tagged for confidence
  case.scad            top level; renders one part, selected by -D part="..."
  lib/shapes.scad      rounded box, gasket groove, boss and insert helpers
  parts/shell_bottom.scad
  parts/shell_top.scad
  parts/pusher.scad
  parts/gasket.scad
  parts/port_bung.scad
  parts/coupon.scad
  Makefile             render targets, plus check
  README.md            print settings, assembly order, BOM, measuring worksheet
  stl/                 rendered output
```

`case.scad` dispatches on a `part` variable so every part is generated from
one consistent parameter set, and on a `battery` variable selecting the
standard or extended pocket.

## Fit-test coupon

A full case is a multi-hour print, and the derived dimensions above mean the
first one is unlikely to be right. `case.scad` renders a `coupon` part: one
corner of the enclosure carrying the PCB pocket edge, one complete button
station with its groove and dome seat, and the port well — roughly twenty
minutes of filament.

The workflow is: measure, render coupon, print, correct `params.scad`, repeat
until it fits, then print the case.

## Verification

Three levels, and it matters which claims each one supports.

**Geometry.** `make check` renders every part through CGAL and asserts the
output reports exactly two volumes — one solid plus the infinite outer volume.
Any other count means the part is non-manifold or has split into pieces, and
the build fails. This proves each part is printable geometry.

**Consistency.** `params.scad` carries `assert()` calls for the relationships
that must hold: groove shallower than the flange, insert shorter than its
boss, button stations clear of the screw bosses, PCB pocket larger than the
PCB by the stated clearance, dome excursion inside the flange width. These
catch a parameter edit that silently breaks the design.

**Fit.** The coupon print, against the actual watch. Nothing in the software
toolchain substitutes for this.

To be explicit about the limits: the first two confirm the model compiles to
closed, printable solids with self-consistent parameters. They cannot confirm
the case fits a Watchy, that the parting line seals, or that the print is
watertight. Those are physical claims, and they need the coupon and a hose
test.

## Out of scope

- Any submersion rating
- A strap: standard 20mm spring-bar lugs, use a commercial strap
- Pressure equalisation venting, which IPX4-5 does not require
- Wireless charging
