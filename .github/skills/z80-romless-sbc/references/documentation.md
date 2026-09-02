# Documentation Lessons

## Information Architecture

- Keep the overview architectural and brief. Operational setup does not belong there.
- Section 7 owns firmware build and flash-provisioning workflow.
- `docs/docs/en/implementation/` is the chronological implementation and
  bring-up plan. Put each explanatory C excerpt inside the phase that first
  needs it, after maintained-source links and before the test plan.
- `reference/glossary.md` owns terminology, `reference/datasheets.md` owns
  component and instrument sources, and `reference/source-index.md` owns the
  maintained-code index.
- Keep `README.md` concise. The complete specification lives under
  `docs/docs/en/` and its hierarchy is defined by `docs/docs/mkdocs.yml`.

## Beginner Accessibility

- Expand concrete signal names, not only categories. Define MREQ#, RD#, WR#, BUSREQ#, BUSACK#, IORQ#, M1#, RFSH#, WAIT#, INT#, NMI#, and peripheral enables individually.
- Explain both the expanded term and its role in this project.
- Distinguish Z80 bus signals, MCP `GPA/GPB` pins, and Pico `GPn` GPIOs.
- Explain active-LOW `#`/`_N`, truth-table `X`, hexadecimal, units, package suffixes, register names, and protocol abbreviations.
- Keep exact package suffixes in the BOM, but warn readers to verify manufacturer ordering codes and package geometry.

## Consistency Rules

A hardware change usually touches more than one section. Search and reconcile:

- inventory and fitted counts
- Pico pin map
- component pin tables
- arbitration equations and PLD source
- breadboard placement and generated SVG
- KiCad schematic, exports, reports, and net manifest
- interface mode tables and diagrams
- operational boundaries
- affected Section 8 phases, C excerpts, tests, and pass gates
- glossary component and signal entries
- datasheet and source indexes

Search for stale part numbers, obsolete architecture names, old section numbers, duplicate commands, and superseded cross-references before finishing.

## MkDocs Site

- Configuration: `docs/docs/mkdocs.yml`; command from that directory:
  `.venv/bin/mkdocs build --strict`.
- Keep implementation phases in `docs/docs/en/implementation/`, hardware design
  in `hardware/`, system behavior in `system/`, and reference material in
  `reference/` or `cpm-dcc/`.
- Keep instrument-specific procedures in dedicated hardware pages. The
  DHO814 page owns analogue capture groups; the DSLogic Plus page owns digital
  channel groups and triggers. Inventory lists and links both instruments.
- Documentation images live in `docs/docs/en/images/`; `npm run layout`
  regenerates the breadboard layout there.
- Mermaid diagrams use the `z80-mermaid` custom fence and
  `docs/docs/en/javascripts/mermaid.mjs`. A strict build must pass, and browser
  validation must prove actual SVG rendering because MkDocs does not execute
  the JavaScript adapter.

## Editing Lessons

- Preserve body content verbatim during structural moves; change only headings, transitions, anchors, and explicit references.
- For very large relocations, use deterministic heading boundaries and validate every moved heading occurs exactly once in its assigned section.
- After the first substantive edit, run the narrowest executable check before continuing.
- Do not confuse existing Markdown lint warnings from compact tables or intentional inline HTML with regressions from a focused documentation edit.
