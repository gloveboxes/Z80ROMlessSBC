---
name: jlcpcb-gerbers
description: 'Create, regenerate, inspect, or fix the JLCPCB Gerber upload ZIP for Z80ROMlessSBC. Use for Gerbers, fabrication archives, four-layer PCB ordering options, missing copper layers, plated/non-plated drill files, and JLCDFM silkscreen-to-pad, silkscreen-to-hole or line-width warnings.'
---

# JLCPCB Gerber Upload

Produce the manufacturing ZIP from this repository's current routed board.
Use the `kicad-pcb` and `z80-romless-sbc` skills alongside this workflow.
Generate files locally; do not upload, order, or approve production without
the user's authorization.

## 1. Preflight

- Inspect the worktree and preserve unrelated changes.
- Have the user save and close this project in KiCad before running the
  pipeline. Check for board/project `.lck` files; do not delete locks or
  overwrite unsaved editor changes.
- The routed source is `hardware/kicad/z80_romless_sbc.kicad_pcb`.
  `scripts/build-kicad-pcb.py` owns generated placement, labels, rules and
  silkscreen fixups. `hardware/kicad/reports/z80_romless_sbc.ses` owns routing.
- Packaging alone must not regenerate placement, reroute, modify nets, or
  change the stack-up. Use the existing full validation/export pipeline.
- Confirm `kicad-cli` is available and `.venv-kicad/bin/python` imports
  `kiutils`. The pipeline detects KiCad's bundled Python on macOS; otherwise
  set `KICAD_PYTHON` to an interpreter with `wx` and `pcbnew`.
- Install `scripts/requirements-kicad.txt` only if the environment or required
  dependencies are missing. Do not upgrade working tools merely to export.

## 2. Generate transactionally

From the repository root:

```sh
PYTHON=.venv-kicad/bin/python npm run kicad
```

Do not use `--schematic-only`. Require a successful exit, not merely an
existing ZIP: old fabrication files remain in place when validation fails.

The full build must pass ERC, manifest and documented-wiring parity,
PCB/DSN/SES reproduction, project-rule checks, zone-refilled DRC, route
budgets, and the silkscreen manufacturing checks below. It stages outputs
and publishes the fabrication package only after all gates succeed.

## 3. Check every production file

Deliver `hardware/kicad/fabrication/z80_romless_sbc-gerbers.zip`.
Its current production manifest is exactly these 12 files:

| ZIP member under `gerbers/` | Function |
| --- | --- |
| `z80_romless_sbc-F_Cu.gtl` | L1 front copper |
| `z80_romless_sbc-GND Plane.g1` | L2 internal GND copper |
| `z80_romless_sbc-+5V Plane.g2` | L3 internal +5 V copper |
| `z80_romless_sbc-B_Cu.gbl` | L4 back copper |
| `z80_romless_sbc-F_Mask.gts` | Top solder mask |
| `z80_romless_sbc-B_Mask.gbs` | Bottom solder mask |
| `z80_romless_sbc-F_Silkscreen.gto` | Top silkscreen |
| `z80_romless_sbc-B_Silkscreen.gbo` | Bottom silkscreen |
| `z80_romless_sbc-Edge_Cuts.gm1` | Outer profile and antenna through-cutout |
| `z80_romless_sbc-job.gbrjob` | Job metadata and layer mapping |

Also require `drill/z80_romless_sbc-PTH.drl` and
`drill/z80_romless_sbc-NPTH.drl`: separate plated and non-plated
millimetre Excellon drills. Do not merge them or omit mounting holes.

No paste layers are needed for this through-hole bare-PCB order. Keep BOM,
placement, IPC-D-356, drill maps/reports, drawings and renders adjacent to
the ZIP, not inside it.

Run this archive audit from the repository root:

```sh
.venv-kicad/bin/python - <<'PY'
import json
from pathlib import Path
import re
from zipfile import ZipFile

root = Path("hardware/kicad/fabrication")
gerbers = (
    "F_Cu.gtl", "GND Plane.g1", "+5V Plane.g2", "B_Cu.gbl",
    "F_Mask.gts", "B_Mask.gbs", "F_Silkscreen.gto",
    "B_Silkscreen.gbo", "Edge_Cuts.gm1", "job.gbrjob",
)
expected = {f"gerbers/z80_romless_sbc-{name}" for name in gerbers}
expected |= {f"drill/z80_romless_sbc-{kind}.drl" for kind in ("PTH", "NPTH")}
with ZipFile(root / "z80_romless_sbc-gerbers.zip") as archive:
    assert archive.testzip() is None, "ZIP CRC failure"
    assert len(archive.namelist()) == len(expected), "Wrong file count"
    assert set(archive.namelist()) == expected, "Wrong production manifest"
    for name in expected:
        data = archive.read(name)
        assert data and data == (root / name).read_bytes(), f"Stale/empty: {name}"
        if name.endswith((".gto", ".gbo")):
            widths = re.findall(rb"%ADD\d+C,([\d.]+)\*%", data)
            assert widths and min(map(float, widths)) >= 0.16, name
        if name.endswith(".drl"):
            assert data.startswith(b"M48\n") and data.rstrip().endswith(b"M30"), name
            assert re.search(rb"^X[-\d.]+Y[-\d.]+$", data, re.M), name
        elif not name.endswith(".gbrjob"):
            assert data.rstrip().endswith(b"M02*"), name
    job = json.loads(archive.read("gerbers/z80_romless_sbc-job.gbrjob"))
    assert job["GeneralSpecs"]["LayerNumber"] == 4
    assert job["GeneralSpecs"]["BoardThickness"] == 1.6
    mapping = {entry["Path"]: entry["FileFunction"] for entry in job["FilesAttributes"]}
    for filename, function in (
        ("F_Cu.gtl", "Copper,L1,Top"),
        ("GND Plane.g1", "Copper,L2,Inr"),
        ("+5V Plane.g2", "Copper,L3,Inr"),
        ("B_Cu.gbl", "Copper,L4,Bot"),
    ):
        assert mapping[f"z80_romless_sbc-{filename}"] == function
print("PASS: complete, current four-layer production ZIP; CRC and silk widths verified")
PY
```

Then audit the exported Gerbers independently of KiCad. This parses the
mask openings (circle, rectangle, obround, `RoundRect` and `FreePoly`
macros) and silk strokes/arcs, and fails on anything under JLCDFM's
0.18 mm good limit:

```sh
tmp=$(mktemp -d) && unzip -q hardware/kicad/fabrication/z80_romless_sbc-gerbers.zip -d "$tmp" \
  && .venv-kicad/bin/python .github/skills/jlcpcb-gerbers/scripts/audit-silk-pad.py "$tmp/gerbers"; \
  rm -rf "$tmp"
```

Expect `danger=0 warning=0` on both sides; the front minimum is currently
0.200 mm (the enforced target). If the audit is changed, prove it still
detects problems by running it on an older, known-bad ZIP from git history.

Inspect the generated 3D preview, profile/cutout, drill report and layer
mapping. Compare dimensions against the outline rather than the Gerber
job's stroke-expanded bounding box.

## 4. Handle JLCDFM findings at the producer

The observed JLCDFM limits for this order are:

| Check | Danger | Warning | Good | Local enforced target |
| --- | --- | --- | --- | --- |
| Silk to drilled hole | <0.13 mm | 0.13 to <0.18 mm | >=0.18 mm | >=0.20 mm |
| Silk to pad (mask opening) | <0.13 mm | 0.13 to <0.18 mm | >=0.18 mm | >=0.20 mm |
| Silk stroke width | <0.10 mm | 0.10 to 0.15 mm (inclusive) | >0.15 mm | >=0.16 mm |

Check the current manufacturer's reported limits; do not assume future
rules are identical. Green counts are passing findings, not warnings.
Zero KiCad DRC alone is not proof of manufacturer DFM acceptance.

Accepted baseline: the ZIP with 0.16 mm strokes and pad-aware gapping
passed JLCDFM with 0 danger and 0 warning in every category (silk-to-pad
50 good, silk-to-hole 10 good, line width 60 good). Treat any new
non-zero red or orange count as a regression.

`check_silkscreen()` in `scripts/build-kicad-pcb.py` checks visible text,
fields and graphics on both sides against all pad/via drills and against
same-side pad copper plus any solder-mask expansion. KiCad DRC passes with
`min_silk_clearance` 0, so it does not catch JLCDFM silk-to-pad findings.
The generator sets printable stroke widths, positions affected labels away
from holes and pads, and gaps straight silk strokes (body outlines and
polarity hatching) around holes and pads by sampling the stroke against the
real pad shape. Never solve a warning by suppressing it, deleting
polarity/pin labels, or blindly clipping text.

For intentional silkscreen repairs:

1. Change the generator, not the Gerbers or generated PCB by hand.
2. Record pre-change `track_signatures()` and require identical routed
   track/via geometry, nets, layers, widths and resolved drill sizes afterward.
3. Use `GetDrillValue()` for inherited via drills: newly imported SES vias
   can have a raw effective-hole width of `-1` before save/reload.
4. Regenerate using KiCad Python and the accepted session:
   `scripts/build-kicad-pcb.py --session hardware/kicad/reports/z80_romless_sbc.ses`,
   then `scripts/build-kicad-pcb.py --check`. Regenerate **before** running
   `npm run kicad`: the pipeline validates the committed PCB, so a changed
   label table otherwise fails with "PCB assembly silkscreen labels are stale".
   Do not autoroute again for silkscreen repairs.
5. Run `scripts/test_kicad_silkscreen.py` with KiCad Python, then repeat the
   full build, archive audit and Gerber silk-to-pad audit. Test boundary
   coordinates depend on `SILK_MIN_STROKE`; update them when it changes.
   Preserve labels, polarity marks and legibility.
6. Prove copper is unchanged by comparing parsed Gerber geometry (copper,
   planes, masks, outline) and comment-stripped drill files with the previous
   ZIP. Raw byte diffs are misleading: KiCad renumbers apertures and stamps
   dates even when geometry is identical.
7. Ask the user to upload the new ZIP and rerun JLCDFM. Do not claim its
   remote checks passed without seeing the new result.

## Lessons learned from the first JLCDFM rounds

- **Read every DFM category, every round.** Fixing one class exposed another:
  silk-to-hole, then silk-to-pad, then line width. Ask for the Details pane
  (layer, value, object) when a count is non-zero; summary counts alone hide
  which footprint is at fault.
- **Thicker strokes eat clearance.** Raising silk from 0.12 to 0.15 mm pushed
  `CP_Radial` polarity hatching and the D1 outline to 0.165-0.18 mm from
  pads. After any stroke change, rerun the pad and hole clearance checks.
- **JLCDFM boundaries are not symmetric.** Exactly 0.15 mm stroke
  (reported as aperture `r5.90551`, a mil diameter) is a *warning*; good
  needs >0.15 mm, so the generator uses 0.16 mm. Clearance at exactly
  0.18 mm is good, but target 0.20 mm for rounding margin.
- **Moving a label can create a new collision.** C4's label between 2.5 mm
  pitch pads was 0.011 mm from copper; its first replacement position hit a
  via. Probe candidate positions against every drill, pad and existing silk
  item before editing `assembly_labels()`.
- **Do not fix silk by deleting it.** Gap straight strokes around the real
  pad shape (sampled along the centreline, so rotated, rounded and custom
  pads work) and drop slivers under 0.05 mm; keep polarity and pin-1 marks.
- **Fail loudly on unsupported geometry.** Arcs, circles and polygons are
  checked but not gapped; move or redesign them rather than silencing the check.
- **Freerouting is not deterministic.** An end-to-end reroute is a useful
  robustness test, but run it in a scratch copy of the worktree (include
  `docs/`, which the interconnect check reads) and never replace the
  accepted SES or production ZIP with its output.

## 5. JLCPCB order review

For the current board:

- **FR-4, four layers, 1.6 mm, 85 x 96 mm**; a detected 96 x 85 mm rotation
  is equivalent. Never approve a two-layer interpretation or 100 x 100 default.
- Layer order: front / GND / +5 V / back. Verify the inner plane mapping.
- **1 oz outer, 0.5 oz inner copper**; standard FR-4 TG135 is suitable.
  Confirm the actual fabrication stack-up; job metadata does not force it.
- Green solder mask and white silkscreen, single PCB, one design.
- **LeadFree HASL** matches the job's finish specification. ENIG is optional,
  not required for this through-hole layout.
- Plugged vias are acceptable for this order; expensive filled-and-capped
  vias are not required. The selected minimum-via option must accommodate
  the actual smallest drill in the generated report.
- Standard +/-0.2 mm outline tolerance and full flying-probe electrical test.
  Recommend **Confirm Production File = Yes** for this first prototype.
- No controlled-impedance requirement, gold fingers, castellations,
  edge plating, press-fit holes or backdrilling. The antenna opening is a
  through-cutout, **not a blind slot**.

Before ordering, require a dry-fit of the purchased J1 USB-C breakout:
its footprint outline was scaled from a vendor photograph. Inspect actual
socket envelopes, OLED height, screw clearance, polarity and cable access;
the render does not contain every physical assembly model.

## Completion

Link the updated ZIP and report the included layers/drills, archive check,
ERC/DRC outcome and any remaining external DFM or physical checks. Register
the ZIP as the requested deliverable. Do not equate fabrication acceptance
with electrical/RF/timing qualification; physical bring-up starts at 1 MHz.
