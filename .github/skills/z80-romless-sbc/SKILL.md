---
name: z80-romless-sbc
description: 'Engineer, review, debug, build, test, or document the Z80ROMlessSBC project. Use for Pico 2 W supervisor firmware, Z80/SRAM/GAL/MCP23S17 buses, voltage translation, breadboard placement, CP/M images, onboard-flash storage, staged bring-up, KiCad checks, README/PDF maintenance, timing qualification, or BOM and pin-map changes.'
argument-hint: 'Describe the hardware, firmware, documentation, or validation task'
user-invocable: true
---

# Z80 ROMless SBC Engineering

Use this workflow to preserve the project's electrical safety, firmware ownership rules, build reproducibility, and documentation consistency.

## Start With Current Sources

1. Treat `README.md` as the engineering and bring-up specification.
2. Treat maintained files under `src/` and `hardware/kicad/` as authoritative implementation sources. README C excerpts explain invariants but are not canonical code.
3. Load [architecture invariants](./references/architecture.md) before changing pins, parts, buses, power, flash, multicore ownership, or timing.
4. Load [validation workflows](./references/validation.md) before building, testing, regenerating artifacts, or declaring a change complete.
5. Load [documentation lessons](./references/documentation.md) before reorganizing the README, editing diagrams, changing terminology, or generating the PDF.

## Procedure

1. Identify the owning surface: electrical design, firmware module, CP/M tooling, staged application, KiCad source, or documentation generator.
2. Read the nearest implementation and its corresponding Section 8 phase before editing.
3. State one local hypothesis and the cheapest check that can falsify it.
4. Make the smallest change that preserves the invariants in the references.
5. Run a focused validation immediately after the first substantive edit.
6. Expand validation according to blast radius:
   - Pin, logic, power, or package changes require schematic, BOM, placement, phase-plan, and documentation consistency checks.
   - Shared firmware changes require affected stage builds and later cumulative stages.
   - Flash or CP/M changes require host image tests, exact geometry checks, and final artifact generation.
   - README changes require link/heading checks and regeneration of the tracked `README.pdf` when rendered content changes.
7. Report measured or tested behavior separately from theoretical limits. Never promote the 20 MHz CPU rating to a system claim.

## Project-Specific Guardrails

- Do not revive superseded SD-card, PSRAM, LVC8T245-carrier, or 74HCT157/74HCT08 architectures without an explicit redesign request.
- Do not connect a 5 V output directly to a Pico GPIO.
- Do not bypass the AHCT244 on GAL-to-SRAM controls.
- Do not share SPI0 across cores or introduce a second bus master on SCK/MOSI.
- Do not treat a physically installed but unpowered 5 V IC as equivalent to an absent socketed device.
- Do not delete `README.pdf`; it is a tracked generated artifact.
- Do not trust package suffixes, TO-92 lead order, breadboard row geometry, or active-low polarity from memory. Verify the exact datasheet and current placement table.
- Do not use CMake Tools extension failures as evidence that the project cannot build; use the terminal workflow in the validation reference.
