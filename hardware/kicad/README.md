# Z80 ROMless SBC KiCad sources

This directory contains the KiCad 10 schematic and the routed two-layer PCB.
The board is 180 x 135 mm and uses socket-compatible through-hole footprints.

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
  -de hardware/kicad/reports/z80_romless_sbc.dsn \
  -do hardware/kicad/reports/z80_romless_sbc.ses \
  -mp 500 -mt 8
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

Signal tracks are 0.30 mm, power tracks are 0.60 mm, and the two clock nets
have 0.40 mm clearance. The bottom GND pour uses thermal reliefs except at a
small set of explicitly generated pads where the route otherwise starves the
thermal spokes.

The DRC-clean layout is not a frequency qualification. Bring up at 1 MHz and
follow the documented staged validation and frequency-qualification procedure.
