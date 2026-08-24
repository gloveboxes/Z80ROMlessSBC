# Documentation Lessons

## Information Architecture

- Keep the overview architectural and brief. Operational setup does not belong there.
- Section 7 owns firmware build and flash-provisioning workflow.
- Section 8 is the chronological implementation and bring-up plan. Put each explanatory C excerpt inside the phase that first needs it, after maintained-source links and before the test plan.
- Appendix A is the glossary, Appendix B datasheets, and Appendix C source index. PDF-generation instructions remain last.
- The web README has a compact top-level TOC near the beginning. The PDF renderer hides `.web-toc` because Pandoc generates a detailed PDF contents section.

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

## PDF Renderer

- Renderer: `scripts/build-readme-pdf.mjs`; command: `npm run pdf`.
- Body and table text use Verdana for a sturdy print x-height. Heading typography remains on the existing Aptos/Segoe UI fallback stack.
- Mermaid diagrams render as vector graphics on dedicated portrait or landscape pages.
- The PDF is tracked. Never delete it as cleanup.
- Generated page count may change after font, table, or section moves; validate physical headings rather than hard-coding historical page numbers.
- TOC headings also appear in extracted PDF text. When checking uniqueness or order, exclude contents pages or identify the physical content page.

## Editing Lessons

- Preserve body content verbatim during structural moves; change only headings, transitions, anchors, and explicit references.
- For very large relocations, use deterministic heading boundaries and validate every moved heading occurs exactly once in its assigned section.
- After the first substantive edit, run the narrowest executable check before continuing.
- Do not confuse existing Markdown lint warnings from compact tables or intentional inline HTML with regressions from a focused documentation edit.
