# Placement and routing discipline

## Physical placement

- Use exact purchased package geometry, not just package family names.
- DIP courtyards model IC packages, not necessarily socket bodies. Check
  socket-body dimensions and placement tolerance separately.
- Preserve polarized-capacitor and diode polarity marks on F.Silkscreen,
  and label J3's power rails. Moving nonpolar bypass outlines to
  F.Fab must not also remove C8-C11 polarity information.
- Label each DIP IC on F.Silkscreen with its part name and reference
  inside the body outline (`IC_LABELS` in `scripts/build-kicad-pcb.py`);
  the bare U reference stays on F.Fab.
- Place every 100 nF bypass within a few millimetres of its target supply pin
  and provide a local GND return.
- Put supply-entry bulk capacitance beside the input; distribute other bulk
  capacitance by power cluster.
- Keep USB, BOOTSEL, SWD, mounting screws, socket extraction, and cable
  insertion mechanically unobstructed.

The current Pico is horizontal at the upper left. Its USB connector faces
outward and the official cable envelope terminates at the left Edge.Cuts. The
antenna sits over a chamfered internal FR-4/copper cutout with manufacturing
margin around the official keepout.

## Nets and layers

- Four copper layers: F.Cu signals, an In1.Cu solid GND plane, an In2.Cu
  solid +5V plane (LT_POWER), and B.Cu signals. Every signal layer is
  adjacent to a plane; never route signal tracks on In1/In2.
- Pico 2 W/RP2350 can run its system/core clock at up to 150 MHz. The generated
  Z80 clock targets PCB qualification through 20 MHz, but Pico-driven nets
  still have fast GPIO edges; do not use the Z80 clock rate as the sole
  signal-integrity model.
- Default signals: 0.30 mm tracks.
- Power (`+5V`, `+3V3`, `VSYS`): 0.60 mm tracks; +5V is normally carried
  by the In2.Cu plane.
- Clock (`PICO_CLK`, `Z80_CLK`): 0.30 mm clearance, locked via-free F.Cu
  preroutes. 0.30 mm is the largest clearance that still lets a 0.30 mm
  track pass between 1.6 mm DIP pads at 2.54 mm pitch; 0.40 mm forces
  long detours around package ends.
- Solid In1.Cu GND and In2.Cu +5V planes with thermal reliefs, inset 0.5 mm
  from Edge.Cuts. Freerouting must keep both inner layers disabled with
  `--router.layers.routable=true,false,false,true`. Use local
  solid connections only where a pad would otherwise have starved spokes.
- Keep copper away from milled edges and antenna cutouts according to project
  design rules and fabricator capability.

## Bus topology

- Address `A0-A15`: shared Z80/SRAM trunk with short pull-network/monitor taps.
- Data `D0-D7`: shared Z80/SRAM/translator trunk, never translator-through-
  translator series routing.
- Pico data `PICO_D0-PICO_D7`: common Pico/translator trunk with RN3 taps.
- Do not add serpentine matching merely to equalize a few millimetres.
- Investigate one-bit outliers, long stubs, extra vias, or a branch that routes
  around the board instead of joining its local trunk.

## Cutouts and connector geometry

The board is 85 x 96 mm. Four 2.7 mm NPTH M2.5 holes are 4 mm from each
adjacent corner edge (77 x 88 mm spacing). A2 reserves the complete 38 x 12 mm OLED body; its local
footprint uses the supplied header offsets, not guessed mounting holes.

J1 is the pictured 21.6 x 12.7 mm USB-C breakout on the top edge, soldered
through all six header pins. Only VBUS/+5V and GND are wired; the other four
carrier pads are isolated. Its outline is scaled from the vendor dimension
photo and its courtyard is flush with the top edge (a generator check);
require a dry-fit before fabrication. Do not invent
module screw holes. Check pin fit, underside insulation and cable access
on the purchased part. J3 is auxiliary; J1/J3 have no source isolation.

Do not confuse a connector cable envelope with the connector shell. Validate
the official footprint geometry against Edge.Cuts. For the Pico:

- USB must face outward at the left edge.
- The antenna keepout must be contained by the physical cutout with at least
  0.25 mm margin per side, including profile tolerance and corner treatment.
- Pico header pads must keep at least 0.6 mm copper-to-edge clearance
  around the cutout (currently 0.68 mm); the generator enforces both.
- Route congestion beside the cutout must not be "fixed" by weakening global
  rules; use local geometry or narrowly justified pad overrides.

## Routing iteration

1. DRC the placed/unrouted board.
2. Export a clean DSN.
3. Autoroute.
4. Import SES and fill zones.
5. Run DRC and the route-length report.
6. Fix structural causes—placement, orientation, pull-network location,
  cutout margin—before adding manual copper.
7. If a generated fixed route is necessary, encode it in Python and include it
  in session/copper signature validation.
