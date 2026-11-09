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

These counts and checks describe the DIP board; use the SMT project's own
manifest and pipeline for that variant.

The build must prove:

- strict schematic ERC
- exact manifest/netlist parity: 71 real nets, 278 schematic endpoints and
  30 fitted PCB footprints with 278 connected endpoints; no test points
  may be added without a request
- footprint values and packages match the manifest, not just net connectivity
- exact documented chip-pair connectivity
- PCB footprint, pad/net, placement, rotation, outline, mounting-hole,
  connector, cutout, keepout, netclass, project-rule, assembly-label, and
  ground-zone invariants
- committed SES reproduces the routed track/via set
- regenerated DSN matches the committed unrouted DSN
- DRC after zone refill has zero violations and zero unconnected items
- silkscreen strokes are at least 0.16 mm, with at least 0.20 mm clearance
  from every pad/via drill and from same-side pad copper plus mask
  expansion; labels and polarity marks remain readable
- `.kicad_pro` remains stable after validation
- Default, Power and Clock netclasses retain their pattern assignments

Never turn a DRC warning into an exclusion merely to finish a route.

Artifacts are generated under a temporary directory and swapped into
`exports/` and `fabrication/` only after all validation passes.

## Fabrication outputs

The package contains:

- F.Cu/In1.Cu/In2.Cu/B.Cu Gerbers
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
