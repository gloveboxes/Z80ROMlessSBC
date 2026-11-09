---
name: jlcpcb-gerbers
description: 'Use for JLCPCB Gerber ZIPs, four-layer fabrication/order review, JLCDFM silk/mask/via/slot fixes, and partial-SMT PCBA BOM/CPL rotations, offsets and component-placement preview validation.'
---

# JLCPCB Manufacturing

Use [KiCad PCB](../kicad-pcb/SKILL.md) for design/routing changes. Commands below
run from the repository root. Do not upload, order or approve production without
authorization.

## Workflow

1. Confirm DIP versus partial-SMT and read the matching sections of the
   [manufacturing reference](./references/manufacturing.md).
   Fetch its official JLCPCB sources for new layouts, rule changes or order
   reviews; snapshots do not override current requirements.
2. Have the user save/close KiCad. Respect `.lck` files and unsaved edits.
   Packaging alone must not regenerate placement, reroute or change nets.
3. Run the matching transactional pipeline and require a successful exit:
   - DIP: `PYTHON=.venv-kicad/bin/python npm run kicad`
   - SMT: `PYTHON=.venv-kicad/bin/python npm run kicad:smt`
   Existing ZIPs are not evidence of success; failed validation preserves old files.
4. Check production files using the reference's archive and
   [silk audits](./references/manufacturing.md#3-check-every-production-file).
   Require four copper layers, masks, silk, outline, job metadata and separate
   PTH/NPTH drills. SMT also needs front paste and matching BOM/CPL.
5. Fix DFM findings in the owning generator, never exported Gerbers or CPL.
   Preserve copper during silk-only repairs and rerun all affected gates.
   Zero KiCad DRC does not establish JLCDFM acceptance.
6. For SMT, follow the
   [placement-preview checks](./references/manufacturing.md#validate-jlcs-component-placement-preview-required)
   for every assembled part. Verify pad centres, pin 1, polarity, connector
   direction and antenna placement against a fresh JLC image after each correction.
7. Apply the variant-specific order checks; report ZIP/BOM/CPL paths, local
   validation, remote DFM/placement evidence and outstanding physical checks.
   Without a reviewed preview, explicitly mark SMT placement unverified.
