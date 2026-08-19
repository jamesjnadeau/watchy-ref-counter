# Watchy Ref — ESP32-S3-MINI-1 / RV-3028-C7 board

A Watchy-compatible watch board carrying an ESP32-S3-MINI-1 module and an
RV-3028-C7 real-time clock, with the accelerometer and the USB-serial bridge
deleted and USB-C in place of micro-USB. Derived from
[sqfmi/watchy-hardware](https://github.com/sqfmi/watchy-hardware) `v2.0`
(MIT), which supplies the e-paper, charging, power and motor circuitry
unchanged.

Design rationale, with the reasoning behind every decision:
[`docs/superpowers/specs/2026-08-18-s3-mini-rv3028-board.md`](../docs/superpowers/specs/2026-08-18-s3-mini-rv3028-board.md).

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
  elec/src/watchy.ato               the board: every part and every connection
  elec/src/parts.ato                passives, discretes, connectors, switches
  elec/src/mcu.ato                  ESP32-S3-MINI-1
  elec/src/rtc.ato                  RV-3028-C7
  elec/src/usb.ato                  USB-C receptacle and the ESD array
  elec/footprints/footprints.pretty custom footprints
  elec/layout/default/              the PCB (see "Layout" below)
  checks/netlist_check.py           design assertions against the netlist
  checks/make_bom.py                fab BOM generator
  checks/run.sh                     build, assert, generate, check the board
  scripts/apply_netlist.py          netlist -> PCB, headless (mutates the board)
  scripts/check_board.py            asserts the PCB matches the netlist
  fab/BOM.csv                       generated, committed
  vendor/                           upstream licence notices
```

## Building and checking it

```bash
python3 -m venv ~/.venvs/ato && ~/.venvs/ato/bin/pip install atopile
ATO=~/.venvs/ato/bin/ato ./checks/run.sh
```

That compiles the sources to `build/default.net`, runs the assertions, and
writes `fab/BOM.csv`. Expect `0 problem(s)` and 54 placements.

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
| bus pull-ups | I2C or `RTC_INT` pulled to `+BATT`, putting 4.2V on S3 inputs |
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
| antenna | the module's keepout allowing pour, or not overhanging the board edge |

Both sets were mutation-tested: the netlist or the board was deliberately
broken in the way each check describes, and the check caught it. A check that
cannot fail is not a check.

**What is not checked:** ERC and DRC. `kicad-cli sch erc` and `pcb drc` need
KiCad 8 or newer, and the newest version installable in the environment this
was built in is 7.0.11. See [`UNVERIFIED.md`](UNVERIFIED.md).

## Layout

`elec/layout/default/watchy-ref-s3.kicad_pcb` started as V2's board, converted
from KiCad 5 and switched to a 4-layer stackup. `scripts/apply_netlist.py` has
been run against it, so it is **connected but not routed**.

KiCad's "Import Netlist" is a GUI dialog and `kicad-cli` has no equivalent, so
that script does the job with `pcbnew`: it deletes the parts the netlist
dropped, replaces the ones whose footprint changed *while preserving V2's
position* (most of those swaps are only a KiCad 5→7 library rename, and losing
placement over a rename would defeat the point of pinning the designators),
adds the new parts, assigns all 249 pads to their 73 nets, lays the GND and
`+3V3` pours, and nudges the small parts it placed off their neighbours.

```bash
/usr/bin/python3.12 scripts/apply_netlist.py --dry-run   # report only
/usr/bin/python3.12 scripts/apply_netlist.py             # write the board
```

It needs the interpreter KiCad's `pcbnew` bindings were built against, not a
venv. `scripts/check_board.py` then verifies the result independently, and
`checks/run.sh` runs it.

### What still needs a human

The script reports courtyard overlaps it could not resolve, with how deep each
one is. Anything under ~0.2mm is KiCad 7's footprints being slightly larger
than KiCad 5's and can be ignored. The rest are real:

| Overlap | Why |
| --- | --- |
| `U1` vs `C12`, `C1`, `R1`, `R7`, `Q5` | The module is 15.4 × 20.5mm where V2 had a 7 × 7mm QFN. It genuinely occupies the space V2's decoupling, the EN network and the RTC pull-up live in. Those five parts have to move. |
| `D4` vs `J2` (2.3mm), `J2` vs `U2` | The USB-C receptacle is bigger than the micro-USB it replaces, at the same edge position. |

Those are design decisions, not arithmetic, which is why the script reports
them instead of guessing. After resolving them:

1. Route `USB_DP`/`USB_DM` as a 90-ohm differential pair over the In1.Cu
   ground plane, first, before anything else claims the space.
2. Keep the charge-pump loop (`L1`, `Q1`, `D1`-`D4`, `R9`) tight — copy V2's
   relative geometry rather than re-inventing it.
3. Fill the zones and run DRC (KiCad 8+).

The module's antenna needs no manual keepout: Espressif's footprint carries a
rule area on `*.Cu` that forbids tracks, vias, pads and pour on every layer,
and `apply_netlist.py` places the module so that area hangs off the top edge.
`check_board.py` asserts both.

### Board outline

The Edge.Cuts outline measures **33.800 × 45.960 mm** on the centreline
(33.950 × 46.110 mm including the 0.15mm line width), measured with `pcbnew`
against the upstream V2 file. sqfmi's published figure and
`docs/superpowers/specs/2026-08-18-waterproof-case-design.md` both say
35.5 × 46.0 mm. **The published figure is wrong**, or at least does not
describe this outline. The case model should be worked against 33.800 × 45.960.

The outline itself has not been changed. Board thickness stays 1.6mm so the
case stack is unchanged. The USB-C cutout is the only mechanical change.

## Fabrication

Nothing here is ready to order — see [`UNVERIFIED.md`](UNVERIFIED.md) — but
when it is:

- 4 layers, 1.6mm, ENIG. The module, the RV-3028 and the 0.5mm FPC connector
  are all fine pitch; HASL is not worth the yield risk.
- Minimum track and clearance 0.15mm, 0.3mm vias with 0.15mm drills.
- Single-sided assembly: every part is on F.Cu.

Order `ESP32-S3-MINI-1`, **not** `-N4R2` — that variant consumes IO26 for
PSRAM. Specify genuine Micro Crystal for the RV-3028-C7.

The display, battery, vibration motor and case assembly are hand work. No
assembly house will finish this board into a watch.

## Firmware

Firmware support for this board does not exist. `RefRtc` short-circuits to the
internal clock on any S3 build and has no RV-3028 driver. The
"Firmware impact" section of the spec lists what is needed; it is a separate
plan and nothing in this directory implements any of it.

## Licences

- The V2 design this is derived from: MIT, `vendor/LICENSE-watchy-hardware.txt`.
- The ESP32-S3-MINI-1 footprint: CC-BY-SA 4.0 from
  [espressif/kicad-libraries](https://github.com/espressif/kicad-libraries),
  `vendor/LICENSE-espressif-kicad-libraries.md`.
