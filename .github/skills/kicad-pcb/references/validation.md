# Validation and fabrication

## Canonical commands

```sh
python3 -m venv .venv-kicad
.venv-kicad/bin/pip install -r scripts/requirements-kicad.txt
PYTHON=.venv-kicad/bin/python npm run kicad

cd docs/docs
.venv/bin/mkdocs build --strict
```

For unrelated regression confidence:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

## Canonical build guarantees

The build must prove:

- strict schematic ERC
- exact manifest/netlist parity
- exact documented chip-pair connectivity
- PCB footprint, pad/net, placement, rotation, outline, mounting-hole,
  connector, cutout, keepout, netclass, project-rule, assembly-label, and
  ground-zone invariants
- committed SES reproduces the routed track/via set
- regenerated DSN matches the committed unrouted DSN
- DRC after zone refill has zero violations and zero unconnected items
- `.kicad_pro` remains stable after validation

Artifacts are generated under a temporary directory and swapped into
`exports/` and `fabrication/` only after all validation passes.

## Fabrication outputs

The package contains:

- F.Cu/B.Cu Gerbers
- F.Mask/B.Mask
- F.Silkscreen/B.Silkscreen
- Edge.Cuts and Gerber job
- separate PTH and NPTH Excellon drills
- grouped BOM
- placement CSV
- IPC-D-356 test netlist
- drill maps/report
- assembly PDF/SVG and 3D PNG

The upload ZIP contains only Gerber and `.drl` production files; maps and
reports remain adjacent review artifacts.

Before ordering, inspect the 3D preview, Edge.Cuts, antenna cutout, socket
spacing, polarity, pin 1, drill sizes, component pitch, board dimensions, and
Gerber archive contents.

## Evidence boundaries

Zero ERC/DRC proves design-rule and connectivity consistency, not physical
operation. Do not claim:

- Wi-Fi antenna performance from keepout geometry alone
- a qualified clock frequency from route length
- power-integrity margin from track width alone
- correct transistor lead order without the purchased-part datasheet

Begin physical bring-up at 1 MHz and follow the project's staged power,
supervisor, bus, SRAM, CPU, I/O, and frequency qualification procedures.
