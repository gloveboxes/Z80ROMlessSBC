# Z80 ROMless SBC KiCad sources

**Current revision:** the schematic uses Pico + Z80-assisted loading, eight
active packages, and fixed HCT32 logic. The PCB, project routing rules,
DSN/SES, renders, and fabrication package below remain the **previous circuit**.
Their migration is deferred. Do not manufacture them for the new schematic
or use the new firmware on the old board. No PLD programmer is needed for the
new breadboard build.

The current schematic includes **J2**, a 1x5, 2.54 mm through-hole peripheral
expansion header: pin 1 GP8, pin 2 GP22, pin 3 GP26/ADC0, pin 4 Pico 3.3 V
output, pin 5 GND. Its footprint is assigned for future PCB migration, but
the PCB and fabrication package have not been updated. All expansion signals
are 3.3 V only. GP8 is reserved for SRAM bank select and has a 10 kOhm
pull-down; GP22/GP26 have no fixed pulls on the board. See the
[expansion pin map and interface limits](../../docs/docs/en/hardware/pin-mapping.md#j2-peripheral-expansion).

The schematic includes the fitted bank-select wiring: **Pico A1 physical
pin 11 (GP8) -> U4 pin 17**, and **U4 pin 3 -> SRAM U2 pin 2 (A16)**.
**R24 is 10 kOhm from Pico physical pin 11 to GND**, on the 3.3 V side of
the level shifter. The former U4 pin 17 and SRAM A16 ground straps are
removed. The existing AHCT244 provides the 5 V address level; no extra IC
is required. R24 is drawn beside the Pico and marked with its attachment
point. The net manifest, schematic exports and BOM include these changes.

The schematic also shows **A2**, the external Adafruit HT16K33 8x8 matrix
operating in Stage 10: `D`/SDA joins GP22 (J2 pin 2), `C`/SCL joins GP26
(J2 pin 3), `+` joins Pico 3.3 V (J2 pin 4), and `-` joins common GND
(J2 pin 5). Symbol pin identifiers are the backpack's printed markings, not
connector positions. A2 is excluded from the PCB and has no footprint;
backpack pull-ups must connect to 3.3 V, never 5 V. See the
[Stage 10 wiring and bench results](../../docs/docs/en/implementation/phase-10-websocket.md#ht16k33-8x8-matrix).

This directory contains the KiCad 10 schematic and the routed four-layer PCB.
The board is 160 x 135 mm and uses socket-compatible through-hole footprints.
The Pico 2 W is horizontal in the bottom-left corner with USB flush to the
left edge; its antenna sits above a dedicated internal FR-4/copper cutout.

This is a separate physical implementation from the three-BB830 prototype.
See the dedicated [PCB documentation](../../docs/docs/en/pcb/index.md) for
its inventory, design considerations, and fabrication references.
The documentation's Phase 0-10 installation sequence describes the new
breadboard circuit. Its current pin map and firmware must not be applied to
the previous PCB. Historical PCB assembly and qualification remain separate.

## Rebuild and validate

From the repository root:

```sh
python3 -m venv .venv-kicad
.venv-kicad/bin/pip install -r scripts/requirements-kicad.txt
PYTHON=.venv-kicad/bin/python npm run kicad
```

The default `scripts/build-kicad.sh --schematic-only` regenerates the schematic,
checks exact netlist parity and documented interconnects, runs strict ERC,
and exports its SVG, PDF, BOM, and netlist. It never loads or modifies the PCB,
project routing rules, or fabrication outputs. Use `PYTHON=.venv-kicad/bin/python`.

The historical full pipeline now requires explicit `--full`; do not run it
for this revision until a PCB migration is separately authorized and completed.

## Historical PCB (Deferred)

The previous PCB route is represented by:

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
