#!/usr/bin/env python3
"""Write the JLCPCB assembly BOM and CPL for the partial-SMT board.

Run with KiCad's Python (pcbnew). Only components whose manifest assembly is
"jlc" are exported; hand-soldered parts (Z80, OLED, headers) are left
out of both files.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parents[1]
KICAD_DIR = ROOT / "hardware" / "kicad-smt"
BOARD_PATH = KICAD_DIR / "z80_romless_sbc_smt.kicad_pcb"
MANIFEST_PATH = KICAD_DIR / "reports" / "net_manifest.json"

# JLCPCB rotation corrections relative to KiCad's library orientation, from
# the community database used by kicad-jlcpcb-tools
# (matthewlai/JLCKicadTools cpl_rotations_db.csv). Every assembled footprint
# must match exactly one rule so a new package cannot silently default to 0.
ROTATION_CORRECTIONS = (
    (r"^SOIC-", 270),
    (r"^SOP-(?!4_|18_)", 270),
    # JLC's preview of C150379/C346929 showed the community +180 degree
    # rule reversing the polarity, so these models match KiCad directly.
    (r"^CP_Elec_", 0),
    # JLC's C393939 preview showed the community +180 degree rule pointing
    # the receptacle mouth inward.
    (r"^USB_C_Receptacle_HRO_TYPE-C-31-M-12$", 0),
    (r"^D_SMA$", 0),
    (r"^[RC]_0603_1608Metric$", 0),
    # C42394205 has no community rule. At 0 degrees JLC's model points its
    # USB end left, 90 degrees counter-clockwise from KiCad's USB-up
    # footprint (seen in the JLC placement preview).
    (r"^RaspberryPi_Pico_W_SMD$", 270),
)

# JLC centres these models on their pad pattern, while the KiCad origin is
# offset toward the receptacle mouth; place them at the copper pad centre.
PAD_CENTRED = (r"^USB_C_Receptacle_HRO_TYPE-C-31-M-12$",)


def placement_position(footprint: pcbnew.FOOTPRINT) -> pcbnew.VECTOR2I:
    name = str(footprint.GetFPID().GetLibItemName())
    if not any(re.search(pattern, name) for pattern in PAD_CENTRED):
        return footprint.GetPosition()
    box = None
    for pad in footprint.Pads():
        if pad.IsOnCopperLayer():
            pad_box = pad.GetBoundingBox()
            if box is None:
                box = pad_box
            else:
                box.Merge(pad_box)
    return box.GetCenter()


def rotation_correction(footprint_name: str) -> int:
    matches = [
        rotation for pattern, rotation in ROTATION_CORRECTIONS
        if re.search(pattern, footprint_name)
    ]
    if len(matches) != 1:
        raise SystemExit(
            f"footprint {footprint_name} needs exactly one JLC rotation rule; "
            f"found {len(matches)}"
        )
    return matches[0]


def reference_key(reference: str) -> tuple[str, int]:
    match = re.fullmatch(r"([A-Z]+)(\d+)", reference)
    if not match:
        raise SystemExit(f"unexpected reference {reference}")
    return match.group(1), int(match.group(2))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", type=Path, default=BOARD_PATH)
    parser.add_argument("--manifest", type=Path, default=MANIFEST_PATH)
    parser.add_argument("--bom", type=Path, required=True)
    parser.add_argument("--cpl", type=Path, required=True)
    args = parser.parse_args()

    details = json.loads(args.manifest.read_text(encoding="utf-8"))[
        "component_details"
    ]
    board = pcbnew.LoadBoard(str(args.board))
    footprints = {fp.GetReference(): fp for fp in board.GetFootprints()}

    assembled = sorted(
        (ref for ref, item in details.items() if item.get("assembly") == "jlc"),
        key=reference_key,
    )
    hand = sorted(
        (ref for ref, item in details.items() if item.get("assembly") == "hand"),
        key=reference_key,
    )
    unknown = sorted(
        ref for ref, item in details.items()
        if item.get("symbol") != "PWR_FLAG"
        and item.get("assembly") not in {"jlc", "hand"}
    )
    if unknown:
        raise SystemExit(f"components without an assembly method: {unknown}")
    if not assembled:
        raise SystemExit("no JLC-assembled components in the manifest")

    groups: dict[tuple[str, str, str], list[str]] = defaultdict(list)
    placements = []
    for reference in assembled:
        item = details[reference]
        lcsc = item.get("lcsc", "")
        if not re.fullmatch(r"C\d+", lcsc):
            raise SystemExit(f"{reference} has invalid LCSC part {lcsc!r}")
        footprint = footprints.get(reference)
        if footprint is None:
            raise SystemExit(f"{reference} is missing from the PCB")
        footprint_name = str(footprint.GetFPID().GetLibItemName())
        expected_name = item["footprint"].split(":")[-1]
        if footprint_name != expected_name:
            raise SystemExit(
                f"{reference} PCB footprint {footprint_name} does not match "
                f"schematic {expected_name}"
            )
        if footprint.GetLayer() != pcbnew.F_Cu:
            raise SystemExit(f"{reference} is not on the top side")
        if footprint.GetValue() != item["value"]:
            raise SystemExit(f"{reference} PCB value does not match schematic")
        groups[(item["value"], footprint_name, lcsc)].append(reference)

        position = placement_position(footprint)
        rotation = (
            footprint.GetOrientationDegrees() + rotation_correction(footprint_name)
        ) % 360
        placements.append((
            reference,
            f"{pcbnew.ToMM(position.x):.4f}mm",
            f"{-pcbnew.ToMM(position.y):.4f}mm",
            "Top",
            f"{rotation:g}",
        ))

    for reference in hand:
        if reference not in footprints:
            raise SystemExit(f"hand-assembled {reference} is missing from the PCB")

    args.bom.parent.mkdir(parents=True, exist_ok=True)
    with args.bom.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, footprint_name, lcsc), references in sorted(
            groups.items(), key=lambda entry: reference_key(entry[1][0])
        ):
            writer.writerow([value, ",".join(references), footprint_name, lcsc])

    args.cpl.parent.mkdir(parents=True, exist_ok=True)
    with args.cpl.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        writer.writerows(placements)

    print(
        f"PASS: JLC BOM has {len(groups)} lines and CPL has "
        f"{len(placements)} top-side placements; {len(hand)} parts are hand "
        f"assembled ({', '.join(hand)})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
