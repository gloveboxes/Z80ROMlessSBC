# Z80 ROMless SBC KiCad sources

This directory contains the KiCad 10 schematic and the routed four-layer PCB.
The board is 160 x 135 mm and uses socket-compatible through-hole footprints.
The Pico 2 W is horizontal in the bottom-left corner with USB flush to the
left edge; its antenna sits above a dedicated internal FR-4/copper cutout.

This is a separate physical implementation from the three-BB830 prototype.
See the dedicated [PCB documentation](../../docs/docs/en/pcb/index.md) for
its inventory, design considerations, and fabrication references.
The documentation's Phase 0-10 installation and jumper-wiring sequence remains
the breadboard plan. Share electrical safeguards, pin assignments, firmware,
and applicable functional tests, but use the PCB BOM and keep PCB assembly
and qualification evidence separate.

## Rebuild and validate

From the repository root:

```sh
python3 -m venv .venv-kicad
.venv-kicad/bin/pip install -r scripts/requirements-kicad.txt
PYTHON=.venv-kicad/bin/python npm run kicad
```

`scripts/build-kicad.sh` regenerates the schematic, validates its netlist,
checks the committed PCB against the generated manifest, runs strict ERC and
DRC, verifies the exact committed routing-session copper and project design
rules, then transactionally replaces the drawing and fabrication outputs.

The PCB route is represented by:

- `z80_romless_sbc.kicad_pcb`, the editable routed board;
- `reports/z80_romless_sbc.dsn`, the unrouted Specctra input generated from
  the deterministic placement;
- `reports/z80_romless_sbc.ses`, the Specctra routing session used by
  `scripts/build-kicad-pcb.py` when regenerating the placed board and route.

`build-kicad-pcb.py` requires KiCad's bundled Python interpreter because it
imports `pcbnew`. Set `KICAD_PYTHON` if it cannot be found automatically.
Normal validation checks the committed board against the regenerated
schematic manifest and placement table; it does not rewrite the routed PCB.

The committed session was produced with Freerouting 2.4.1 under Java 25:

```sh
java -jar freerouting-2.4.1.jar \
  --gui.enabled=false \
  --router.layers.routable=true,false,true,true \
  -de hardware/kicad/reports/z80_romless_sbc.dsn \
  -do hardware/kicad/reports/z80_romless_sbc.ses \
  -mp 500 -mt 1
```

After routing, regenerate the board explicitly with:

```sh
KICAD_PYTHON=/path/to/kicad/python \
  /path/to/kicad/python scripts/build-kicad-pcb.py \
  --session hardware/kicad/reports/z80_romless_sbc.ses
```

## Manufacturing outputs

`fabrication/z80_romless_sbc-gerbers.zip` contains the copper, mask,
silkscreen, edge-cut, plated-drill, and non-plated-drill files. The adjacent
BOM, placement CSV, IPC-D-356 netlist, drill reports, and drawings support
assembly and inspection.

Before ordering, verify actual component dimensions and pinouts against the
selected parts, especially the Pico headers, DIP sockets, electrolytic
capacitors, terminal block, diode, and 2N3904.

J1 is marked `+5V` and `GND`. The polarized C9-C12 footprints retain their
positive marks and negative-side shading on the manufactured silkscreen.
Do not remove these markings to hide a drawing collision. The 5.76 mm U1/U4
pin-row separation is not a measured socket-body gap, and the container's
board-only 3D render does not validate uninstalled component or socket models.

Fit R23-R26 as 4.7 kOhm, not 10 kOhm, to guarantee startup LOW levels against
the supported ATF22V10B's input pull-ups. With the populated board, apply
external +5 V before USB and disconnect USB before removing external +5 V.
Do not operate or program it from USB alone: U9's AHCT245 data ports lack
power-off isolation. Uncontrolled external-supply loss with USB connected
requires additional hardware protection.

Signal tracks are 0.30 mm, power tracks are 0.60 mm, and the two clock nets
have 0.40 mm clearance. In1.Cu is reserved as a solid GND plane; Freerouting
is allowed to route only F.Cu, In2.Cu, and B.Cu. The plane uses thermal reliefs
except at a small set of explicitly generated pads where the route would
otherwise starve the thermal spokes.

The DRC-clean layout is not a frequency qualification. Bring up at 1 MHz using
a PCB-specific assembly and bring-up procedure. Reuse applicable firmware
tests and receive-pin measurements from the breadboard plan, but record
results for this PCB separately before claiming its 8 MHz design target.
