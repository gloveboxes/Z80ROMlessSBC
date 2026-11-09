---
name: kicad-pcb
description: 'Use for KiCad schematic/PCB/project files, footprints and placement, four-layer routing, Pico USB/antenna geometry, pcbnew, DSN/SES/Freerouting, ERC/DRC, trace-length analysis and fabrication validation.'
argument-hint: "Describe the KiCad schematic, PCB, routing, fabrication, or validation task"
user-invocable: true
---

# Z80 ROMless SBC KiCad PCB

For nets, parts, pins, voltage, power or timing changes, also use
[firmware/hardware engineering](../z80-romless-sbc/SKILL.md) and its architecture
gates. Commands run from the repository root.

## Select the Board

The references below describe the DIP board in `hardware/kicad/`:

| Owner | Source |
| --- | --- |
| Connectivity, parts and net manifest | `scripts/build-kicad-schematic.py` |
| Placement, rules, silk, zones and invariant checks | `scripts/build-kicad-pcb.py` |
| Routed board | `hardware/kicad/z80_romless_sbc.kicad_pcb` |
| Accepted routing / normalized unrouted input | `hardware/kicad/reports/z80_romless_sbc.ses` / `.dsn` |
| Transactional validation/export | `scripts/build-kicad.sh` |

For partial-SMT, use the separate
[SMT project guide](../../../hardware/kicad-smt/README.md) and its `*-smt`
generators/pipeline; never apply DIP-specific counts or geometry to that board.

## Workflow

1. Read [tooling/formats](./references/tooling.md) and
   [layout/routing](./references/layout-routing.md) for design changes.
   Change generators, not generated connectivity or untracked copper fixups.
2. Before routing, generate clean placement with KiCad Python and run DRC:
   only unconnected items may remain. Route F.Cu/B.Cu only, preserving continuous
   In1 GND and In2 +5V planes; import the accepted SES through the generator.
3. Remeasure routes with the reference's report command; compare source-specific
   paths with the [route baseline](./references/current-route.md).
4. Run `PYTHON=.venv-kicad/bin/python npm run kicad` for DIP, or `npm run kicad:smt`
   with the same `PYTHON` for SMT. Require the
   [validation gates](./references/validation.md): zero ERC/DRC/unconnected items,
   exact source/session parity and transactional fabrication output.
   Never suppress a DRC warning just to finish a route.
5. For PCB site-documentation changes, follow [MkDocs documentation](../mkdocs-docs/SKILL.md). For manufacturer files,
   DFM, BOM/CPL or order review, use [JLCPCB manufacturing](../jlcpcb-gerbers/SKILL.md).
6. Report local checks separately from physical qualification; bring-up starts
   at 1 MHz, and the 20 MHz part rating is not a board qualification.
