---
name: z80-romless-sbc
description: 'Use for Pico 2 W/RP2350 firmware, VS Code build/test/emulator/import tasks, Stage 11 ATtiny85 ISP/test Picos, multicore/IRQ ownership, USB/Wi-Fi/WebSocket performance, CP/M BIOS/images/flash layout, UF2 flashing, assisted SRAM loading, HCT32/voltage/pin/BOM changes, staged bring-up, and DSLogic timing qualification. For MkDocs documentation use the mkdocs-docs skill.'
argument-hint: 'Describe the hardware, firmware or validation task'
user-invocable: true
---

# Z80 ROMless SBC Engineering

## Load Only What the Task Needs

| Task | Required reference |
| --- | --- |
| Firmware, concurrency, performance, hardware, pins, power or timing | [Architecture invariants](./references/architecture.md) |
| Builds, tests, image generation or qualification | [Validation workflows](./references/validation.md) |
| VS Code task selection, emulator refresh or Altair import | [Task selection](./references/validation.md#vs-code-task-selection) |
| Stage 11 separate Pico ISP or I2C test deployment | [Stage 11 flashing](./references/firmware-flashing.md#stage-11-separate-pico-tasks) |
| Authorized firmware deployment or USB supervisor access | [Flashing procedure](./references/firmware-flashing.md) |

Use [disk flashing](../flash-cpm-disks/SKILL.md) for disk-only replacements,
[MkDocs documentation](../mkdocs-docs/SKILL.md) for site pages,
[banked applications](../banked-memory-apps/SKILL.md) for DCC/BANKSDK work,
[KiCad PCB](../kicad-pcb/SKILL.md) for layout/routing,
[JLCPCB](../jlcpcb-gerbers/SKILL.md) for manufacturing, and
[Rigol scope](../rigol-scope/SKILL.md) for DHO814 captures.

## Workflow

1. Read the owning implementation and corresponding implementation-phase page.
   Identify core, IRQ/callback context, shared state and module boundaries.
2. State a testable hypothesis and the cheapest falsifying check. Performance
   work needs a baseline, acceptance metric and work/memory budget.
3. Preserve core 0's Z80 ownership and core 1's USB/network/storage ownership.
   Keep trap work bounded; retain settling guards, flash-safe handshakes,
   disk-first scheduling and callback-safe teardown.
4. Make a focused change and validate immediately, using the relevant validation
   section. Build affected and later cumulative stages for shared firmware;
   exercise real boundary code for transport/concurrency changes.
5. Reconcile schematic, BOM, placement and phase documentation for hardware
   changes. Check image geometry and regenerate artifacts for CP/M changes.
   Follow the mkdocs-docs skill for site changes.
6. Report observed results separately from theory and untested behavior.
   Neither a CPU part rating nor a host test qualifies the board at 20 MHz.

Never bypass voltage translation, hardware ownership or safe power sequencing.
Flashing requires authorization and saved work; use the maintained tools and
same-shell virtual-environment preflight in the flashing reference.
