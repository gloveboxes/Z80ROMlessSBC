# PCB Design

The PCB is a **separate physical implementation** of the Z80 ROMless SBC:
a 160 x 135 mm, four-layer, through-hole board with socket-compatible active
devices. It shares the breadboard's pin assignments, bus ownership, voltage
translation, electrical safeguards, and maintained firmware.

Pico firmware remains [breadboard-first and shared](../system/firmware-build.md#breadboard-first-shared-pico-firmware).
The PCB does not introduce a separate source tree or faster startup defaults.

The [Phase 0-10 implementation plan](../implementation/index.md) remains the
three-BB830 breadboard build. Do not use its socket-row positions or
incremental jumper-wiring instructions as PCB assembly instructions.

| Page | Covers |
| --- | --- |
| [PCB inventory](inventory.md) | PCB-specific fitted parts, sockets, connectors, mechanical items, and differences from the breadboard BOM |
| [Design considerations](design-considerations.md) | Layer stack, component placement, power sequencing, routing, timing, and assembly constraints |
| [Shared electrical reference](../hardware/pin-mapping.md) | Exact IC pin assignments and SRAM-control logic |
| [Shared firmware](../system/firmware-build.md) | Stage programs, CP/M images, and Pico provisioning |

## Design status and boundaries

The current generated design has 51 physical schematic components, 79 real
nets, and 341 component pin endpoints. Four mounting-hole footprints are
additional mechanical features. The ERC-only power marker is not a fitted
part.

The routed PCB and its fabrication files have passed the repository's
connectivity, layout, and generation checks. **This is not a
hardware-qualified design.** Start physical bring-up at 1 MHz. Qualification
through at least 8 MHz is a PCB design target, not a demonstrated operating
speed or a change to the breadboard's target.

Electrical requirements and applicable firmware tests are shared, but PCB
assembly and qualification records must be separate. A passing breadboard
does not qualify the PCB, and a clean PCB does not qualify the breadboard.
An independently documented PCB assembly/bring-up sequence and measured
timing evidence are still required before claiming a working hardware build.

## Editable sources and drawings

Paths below are relative to the repository root.

| Artifact | Purpose |
| --- | --- |
| [KiCad project](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/z80_romless_sbc.kicad_pro) | Project configuration and design rules |
| [Native schematic](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/z80_romless_sbc.kicad_sch) | Electrical connections and fitted component values |
| [Routed PCB](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/z80_romless_sbc.kicad_pcb) | Editable placement, copper, outline, and silkscreen |
| [Schematic PDF](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/exports/z80_romless_sbc.pdf) | Full electrical drawing |
| [PCB SVG](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/exports/z80_romless_sbc-pcb.svg) / [PDF](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/exports/z80_romless_sbc-pcb.pdf) | Copper, silkscreen, and outline drawings |
| [Board preview](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/exports/z80_romless_sbc-pcb.png) | 3D board rendering; absent component/socket models cannot establish assembly clearance |
| [DRC report](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/reports/z80_romless_sbc-drc.json) / [board statistics](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/reports/z80_romless_sbc-stats.json) | Generated layout evidence |

## Source ownership and regeneration

The schematic generator, `scripts/build-kicad-schematic.py`, owns component
values, exact pin/net assignments, symbols, and the net manifest.
`scripts/build-kicad-pcb.py` owns placement, board geometry, netclasses,
silkscreen, ground-plane setup, and PCB invariants.

The normalized unrouted input is
`hardware/kicad/reports/z80_romless_sbc.dsn`; the accepted routing session is
`hardware/kicad/reports/z80_romless_sbc.ses`. Regeneration must reproduce that
session's exact copper, not silently substitute an independently edited route.

Follow the checkout's
[KiCad generation instructions](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/README.md).
Keep the project closed while generating or replacing its files. The separate
local container checkout, `pcb-z80`, has its own `scripts/container-run`
workflow; it is not a step in the breadboard firmware build.

Before accepting regenerated outputs, require zero ERC violations, zero DRC
violations and unconnected items, matching pin/net and component-value
manifests, and matching routing-session copper. Publish the drawings, BOM,
and fabrication package together only after these gates pass.

## Fabrication package

The manufacturing outputs are under `hardware/kicad/fabrication/`.

| Artifact | Purpose |
| --- | --- |
| [Gerber/drill ZIP](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/fabrication/z80_romless_sbc-gerbers.zip) | Copper, mask, silkscreen, board edges, and plated/non-plated drills |
| [Generated BOM](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/fabrication/z80_romless_sbc-bom.csv) | Authoritative fitted component references, values, and footprints |
| [Placement CSV](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/fabrication/z80_romless_sbc-positions.csv) | Through-hole component positions |
| [IPC-D-356 netlist](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/fabrication/z80_romless_sbc.ipc) | Electrical manufacturing-test netlist |

Use a four-layer, 1.6 mm FR-4 fabrication stack with plated through holes and
both solder masks. Confirm the manufacturer's actual stack-up, internal
cutout process, and drill capabilities before ordering. Review the
[inventory](inventory.md) against purchased parts and the
[mechanical constraints](design-considerations.md#placement-and-mechanical-clearance);
the generated files alone do not establish that every purchased socket or
component fits.
