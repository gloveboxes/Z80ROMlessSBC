---
name: mkdocs-docs
description: 'Use when writing, editing, restructuring or reviewing the Z80ROMlessSBC MkDocs site (docs/docs/en, mkdocs.yml, README.md): page content, navigation, Mermaid diagrams, images, C excerpts, glossary and terminology, and strict-build/browser validation. Enforces concise docs that describe only the current design, never legacy, superseded or historical designs.'
argument-hint: 'Describe the documentation page or change'
user-invocable: true
---

# MkDocs Documentation

The site is `docs/docs/en/`, configured by `docs/docs/mkdocs.yml`. `README.md`
is only the concise entry point.

## Rules

- **Concise.** Say each thing once, in the page that owns it. Link instead of
  repeating. Cut filler, narration and duplicated commands.
- **Current design only.** This project is greenfield with one user. Never
  mention legacy, superseded, retired, former, older or "no longer" designs,
  implementations, phases or tools. No history, migration notes or
  compatibility paths. If something changed, rewrite the page as if the
  current design was always the design.
- **No stale leftovers.** When a design changes, remove the old part numbers,
  section numbers, commands and cross-references rather than annotating them.
- **Source of truth.** Maintained code in `src/` and `hardware/kicad/` is
  canonical; C excerpts only explain invariants and sit next to a source link.

## Workflow

1. Find the owning page using the information architecture in
   [conventions](./references/conventions.md); edit there, not in a copy.
2. For hardware changes, reconcile every item in its consistency list.
3. Preserve body text verbatim in structural moves; change only headings,
   transitions, anchors and references.
4. Validate after the first substantive edit using
   [validation](./references/validation.md): `npm run layout` when images
   change, strict MkDocs build, `git diff --check`, and a desktop plus 390 px
   browser check for Mermaid and navigation changes.
5. Before finishing, search the changed pages for history wording
   ("legacy", "superseded", "retired", "former", "older", "no longer",
   "previously", "historical").

Use [z80-romless-sbc](../z80-romless-sbc/SKILL.md) for the firmware or hardware
behind a page and [kicad-pcb](../kicad-pcb/SKILL.md) for PCB documentation
sources.
