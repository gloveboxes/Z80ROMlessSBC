# Placement and routing discipline

## Physical placement

- Use exact purchased package geometry, not just package family names.
- DIP courtyards model IC packages, not necessarily socket bodies. Check
  socket-body dimensions and placement tolerance separately.
- Preserve polarized-capacitor and diode polarity marks on F.Silkscreen,
  and label the power connector's rails. Moving nonpolar bypass outlines to
  F.Fab must not also remove C9-C12 polarity information.
- Place every 100 nF bypass within a few millimetres of its target supply pin
  and provide a local GND return.
- Put supply-entry bulk capacitance beside the input; distribute other bulk
  capacitance by power cluster.
- Keep probe loops accessible after sockets and the Pico are installed.
- Keep USB, BOOTSEL, SWD, mounting screws, socket extraction, and cable
  insertion mechanically unobstructed.

The current Pico is horizontal in the bottom-left. Its USB connector faces
outward and the official cable envelope terminates at the left Edge.Cuts. The
antenna sits over a chamfered internal FR-4/copper cutout with manufacturing
margin around the official keepout.

## Nets and layers

- Four copper layers: F.Cu signals, an In1.Cu solid GND plane, In2.Cu
  signals, and B.Cu signals.
- Pico 2 W/RP2350 can run its system/core clock at up to 150 MHz. The generated
  Z80 clock targets qualification through at least 8 MHz, but Pico-driven nets
  still have fast GPIO edges; do not use the Z80 clock rate as the sole
  signal-integrity model.
- Default signals: 0.30 mm tracks.
- Power (`+5V`, `+3V3`, `VSYS`): 0.60 mm tracks.
- Clock (`PICO_CLK`, `Z80_CLK`): 0.40 mm clearance.
- Solid In1.Cu GND plane with thermal reliefs. Freerouting must keep In1.Cu
  disabled with `--router.layers.routable=true,false,true,true`. Use local
  solid connections only where a pad would otherwise have starved spokes.
- Keep copper away from milled edges and antenna cutouts according to project
  design rules and fabricator capability.

## Bus topology

- Address `A0-A15`: shared Z80/SRAM/MCP trunk with short pull-network taps.
- Data `D0-D7`: shared Z80/SRAM/translator trunk, never translator-through-
  translator series routing.
- Pico data `PICO_D0-PICO_D7`: common Pico/translator trunk with RN3 taps.
- Do not add serpentine matching merely to equalize a few millimetres.
- Investigate one-bit outliers, long stubs, extra vias, or a branch that routes
  around the board instead of joining its local trunk.

## Cutouts and connector geometry

Do not confuse a connector cable envelope with the connector shell. Validate
the official footprint geometry against Edge.Cuts. For the Pico:

- USB must face outward at the left edge.
- The antenna keepout must be contained by the physical cutout, including
  profile tolerance and corner treatment.
- Header pads must retain copper-to-edge clearance around the cutout.
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
