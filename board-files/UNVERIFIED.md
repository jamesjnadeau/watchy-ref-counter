# What has not been checked

`checks/run.sh` proves the netlist matches the spec. That is the only thing
that has been proved. Everything below is open, and every item is blocking
before this board is sent to a fab house.

- **The RV-3028-C7 footprint is provisional.** `elec/footprints/footprints.pretty/RV-3028-C7.kicad_mod` was drawn from the C7 package outline (3.20 x 1.50 x 0.80mm, 8 terminals, 4 per long side) with a reconstructed land pattern: 0.90mm pitch, 0.50 x 0.50mm pads, rows 1.20mm centre to centre. Those numbers were NOT read off the datasheet's recommended-solder-pad drawing -- network egress to microcrystal.com and every datasheet mirror is blocked in the environment this was built in. Get the datasheet, correct the footprint, then print it at 1:1 and lay the real part on it. A footprint wrong by half a pitch is invisible on screen and obvious on paper.
- **The RV-3028-C7 pin numbering is provisional for the same reason.** The pin functions (1 CLKOUT, 2 INT, 3 SCL, 4 SDA, 5 VSS, 6 VBACKUP, 7 VDD, 8 EVI) come from the spec; which physical corner is pin 1, and whether numbering runs counter-clockwise, was assumed when drawing the footprint. Confirm both against the datasheet.
- **No ERC has been run.** atopile has no ERC, and `kicad-cli sch erc` needs KiCad 8+; only 7.0.11 is installable here. Nothing has checked for driven-output conflicts or unconnected pins beyond what the netlist assertions cover.
- **No DRC has been run, because there is no layout yet.** `elec/layout/default/watchy-ref-s3.kicad_pcb` is V2's board converted to the modern format and switched to a 4-layer stackup. The new parts have not been placed and nothing has been rerouted. See the README for what the layout step involves.
- **The `VBACKUP` decision is reasoned, not measured.** Tying `VBACKUP` to `GND` rather than to a rail is argued from the direct-switchover behaviour described in the RV-3028-C7 application manual. Read that section before committing to it.
- **The USB-C receptacle is a footprint choice, not a sourcing decision.** `Connector_USB:USB_C_Receptacle_XKB_U262-16XN-4BVC11` is a common 16-pin part, but the mechanical fit against V2's micro-USB cutout position has not been checked, and neither has the case.
- **The ESP32-S3-MINI-1 land pattern is Espressif's own** (github.com/espressif/kicad-libraries, CC-BY-SA 4.0), which is about as good as a sourced footprint gets, but it has still never been through a reflow oven on this board.
- **Nothing here has been fabricated, powered or tested.** No claim in this repository should be read as saying the design works.
