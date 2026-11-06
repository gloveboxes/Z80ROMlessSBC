#!/usr/bin/env python3
"""Generate the placed four-layer partial-SMT PCB from the schematic net manifest.

This is the JLCPCB-assembly variant. It writes only to ``hardware/kicad-smt``
and never modifies the Rev B board in ``hardware/kicad``.

Run this script with KiCad's bundled Python interpreter so the ``pcbnew``
module is available.  Pass ``--session`` to import a Specctra routing session
after generating the deterministic placement.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from pathlib import Path

try:
    import wx
    _WX_APP = wx.App(False)
    import pcbnew
except ImportError as exc:
    raise SystemExit(
        "pcbnew is unavailable; run with KiCad's bundled Python interpreter"
    ) from exc


REPO_ROOT = Path(__file__).resolve().parent.parent
KICAD_DIR = REPO_ROOT / "hardware" / "kicad-smt"
MANIFEST_PATH = KICAD_DIR / "reports" / "net_manifest.json"
BOARD_PATH = KICAD_DIR / "z80_romless_sbc_smt.kicad_pcb"
PROJECT_PATH = KICAD_DIR / "z80_romless_sbc_smt.kicad_pro"
DSN_PATH = KICAD_DIR / "reports" / "z80_romless_sbc_smt.dsn"
SESSION_PATH = KICAD_DIR / "reports" / "z80_romless_sbc_smt.ses"

BOARD_LEFT = 5.0
BOARD_TOP = 5.0
BOARD_WIDTH = 98.3
BOARD_HEIGHT = 60.0
BOARD_RIGHT = BOARD_LEFT + BOARD_WIDTH
BOARD_BOTTOM = BOARD_TOP + BOARD_HEIGHT
# Pico 2 W: rotated 180 degrees, pin 1's module hole centre at this point,
# USB flush with the bottom edge and the antenna end over an enclosed window.
# A full-width FR4 bridge along the top edge keeps the board rigid; it
# carries no copper. The JLC-assembled SMD footprint's origin is the module
# centre, and its castellated pads extend 0.8 mm beyond the hole rows.
PICO_X = BOARD_LEFT + 27.0
PICO_Y = BOARD_BOTTOM - 2.8
PICO_FOOTPRINT = "RaspberryPi_Pico_W_SMD"
PICO_PIN1_OFFSET = (8.89, 24.13)
# Library zones that suggest an RF board edge; the enclosed antenna window
# and bridge keepout replace them.
PICO_DROPPED_ZONES = {"RF Copper Keep Out", "Board Keep Out Suggestion RF"}
_DETACHED_ITEMS: list[object] = []
ANTENNA_LEFT = PICO_X - 16.30
ANTENNA_RIGHT = PICO_X - 1.50
ANTENNA_BRIDGE = 4.0
ANTENNA_TOP = BOARD_TOP + ANTENNA_BRIDGE
ANTENNA_BOTTOM = PICO_Y - 39.80
ANTENNA_CORNER_CHAMFER = 1.00
# HRO TYPE-C-31-M-12: the receptacle mouth is 3.65 mm from its origin.
USB_C_MOUTH_OFFSET = 3.65
ANTENNA_KEEPOUT_MARGIN = 0.25
ANTENNA_PAD_EDGE_CLEARANCE = 0.60
SIGNAL_CLEARANCE = 0.20
SIGNAL_TRACK_WIDTH = 0.30
SIGNAL_VIA_DIAMETER = 0.80
SIGNAL_VIA_DRILL = 0.40
POWER_TRACK_WIDTH = 0.60
LOGIC_POWER_TRACK_WIDTH = 0.40
POWER_VIA_DIAMETER = 1.00
POWER_VIA_DRILL = 0.50
CLOCK_CLEARANCE = 0.30
# USB-C CC pads are 0.3 mm wide on a 0.5 mm pitch; the CC pull-downs carry
# microamps, and 0.15 mm stays inside JLCPCB's 0.09 mm four-layer minimum.
USB_TRACK_WIDTH = 0.15
MIN_TRACK_WIDTH = 0.15
MIN_VIA_DIAMETER = 0.60
MIN_VIA_ANNULAR = 0.15
MIN_MICROVIA_DIAMETER = 0.30
MIN_MICROVIA_DRILL = 0.10
COPPER_EDGE_CLEARANCE = 0.20
SILK_MIN_STROKE = 0.16
SILK_HOLE_CLEARANCE = 0.20
SILK_PAD_CLEARANCE = 0.20


# name, clearance, track width, via diameter, via drill, assigned nets
NETCLASSES = (
    ("Default", SIGNAL_CLEARANCE, SIGNAL_TRACK_WIDTH,
     SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, ()),
    ("Power", SIGNAL_CLEARANCE, POWER_TRACK_WIDTH,
     POWER_VIA_DIAMETER, POWER_VIA_DRILL, ("+5V", "VSYS")),
    # The Pico regulator's 3V3 rail feeds only the LVC buffers, pulls and
    # OLED; 0.4 mm lets it share the bottom-edge OLED corridor.
    ("Power3V3", SIGNAL_CLEARANCE, LOGIC_POWER_TRACK_WIDTH,
     SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, ("+3V3",)),
    ("Clock", CLOCK_CLEARANCE, SIGNAL_TRACK_WIDTH,
     SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, ("PICO_CLK", "Z80_CLK")),
    ("USB", SIGNAL_CLEARANCE, USB_TRACK_WIDTH,
     SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, ("USB_CC1", "USB_CC2")),
)


def mm(value: float) -> int:
    # pcbnew.FromMM truncates, so 32.8 mm would become 32799999 nm.
    return round(value * pcbnew.FromMM(1))


def vec(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def find_footprint_root() -> Path:
    configured = os.environ.get("KICAD_FOOTPRINT_DIR")
    candidates = [
        Path(configured) if configured else None,
        Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"),
        Path("/usr/share/kicad/footprints"),
        Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints"),
    ]
    for candidate in candidates:
        if candidate is not None and candidate.is_dir():
            return candidate
    raise SystemExit(
        "KiCad footprint libraries not found; set KICAD_FOOTPRINT_DIR"
    )


def project_netclass(
    name: str,
    clearance: float,
    track_width: float,
    via_diameter: float,
    via_drill: float,
    priority: int,
) -> dict[str, object]:
    return {
        "bus_width": 12,
        "clearance": clearance,
        "diff_pair_gap": 0.25,
        "diff_pair_via_gap": 0.25,
        "diff_pair_width": 0.2,
        "line_style": 0,
        "microvia_diameter": MIN_MICROVIA_DIAMETER,
        "microvia_drill": MIN_MICROVIA_DRILL,
        "name": name,
        "pcb_color": "rgba(0, 0, 0, 0.000)",
        "priority": priority,
        "schematic_color": "rgba(0, 0, 0, 0.000)",
        "track_width": track_width,
        "tuning_profile": "",
        "via_diameter": via_diameter,
        "via_drill": via_drill,
        "wire_width": 6,
    }


def sync_project_rules() -> None:
    project = json.loads(PROJECT_PATH.read_text(encoding="utf-8"))
    rules = project["board"]["design_settings"]["rules"]
    rules.update({
        "min_copper_edge_clearance": COPPER_EDGE_CLEARANCE,
        "min_microvia_diameter": MIN_MICROVIA_DIAMETER,
        "min_microvia_drill": MIN_MICROVIA_DRILL,
        "min_track_width": MIN_TRACK_WIDTH,
        "min_via_annular_width": MIN_VIA_ANNULAR,
        "min_via_diameter": MIN_VIA_DIAMETER,
        "min_text_thickness": SILK_MIN_STROKE,
    })
    defaults = project["board"]["design_settings"]["defaults"]
    defaults["silk_line_width"] = SILK_MIN_STROKE
    defaults["silk_text_thickness"] = SILK_MIN_STROKE
    project["net_settings"]["classes"] = [
        project_netclass(
            name, clearance, width, via, drill,
            2147483647 if name == "Default" else -1,
        )
        for name, clearance, width, via, drill, _ in NETCLASSES
    ]
    project["net_settings"]["netclass_patterns"] = [
        {"netclass": name, "pattern": net_name}
        for name, *_, nets in NETCLASSES
        for net_name in nets
    ]
    PROJECT_PATH.write_text(
        json.dumps(project, indent=2) + "\n",
        encoding="utf-8",
    )


def check_project_rules() -> None:
    project = json.loads(PROJECT_PATH.read_text(encoding="utf-8"))
    rules = project["board"]["design_settings"]["rules"]
    expected_rules = {
        "min_copper_edge_clearance": COPPER_EDGE_CLEARANCE,
        "min_microvia_diameter": MIN_MICROVIA_DIAMETER,
        "min_microvia_drill": MIN_MICROVIA_DRILL,
        "min_track_width": MIN_TRACK_WIDTH,
        "min_via_annular_width": MIN_VIA_ANNULAR,
        "min_via_diameter": MIN_VIA_DIAMETER,
        "min_text_thickness": SILK_MIN_STROKE,
    }
    for name, expected in expected_rules.items():
        if abs(float(rules.get(name, -1)) - expected) > 0.001:
            raise SystemExit(f"project rule {name} is stale")
    expected_classes = {
        name: (clearance, width, via, drill)
        for name, clearance, width, via, drill, _ in NETCLASSES
    }
    actual_classes = {
        entry["name"]: (
            float(entry["clearance"]),
            float(entry["track_width"]),
            float(entry["via_diameter"]),
            float(entry["via_drill"]),
        )
        for entry in project["net_settings"]["classes"]
    }
    if actual_classes != expected_classes:
        raise SystemExit("project netclass definitions are stale")
    actual_patterns = {
        (entry["pattern"], entry["netclass"])
        for entry in project["net_settings"]["netclass_patterns"]
    }
    expected_patterns = {
        (net_name, name) for name, *_, nets in NETCLASSES for net_name in nets
    }
    if actual_patterns != expected_patterns:
        raise SystemExit("project netclass assignments are stale")


MODEL_DIR = KICAD_DIR / "3dmodels"
STOCK_MODEL_DIR = Path(os.environ.get(
    "KICAD10_3DMODEL_DIR",
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels",
))
Box = tuple[float, float, float, float, float, float]
MODEL_COLOURS = {
    "body": (0.10, 0.10, 0.11),
    "lead": (0.78, 0.78, 0.80),
    "gold": (0.85, 0.68, 0.25),
    "shell": (0.75, 0.76, 0.78),
    "pcb": (0.05, 0.25, 0.55),
    "glass": (0.03, 0.03, 0.05),
    "active": (0.08, 0.12, 0.30),
    "mark": (0.55, 0.55, 0.55),
}


def sop32_model() -> dict[str, list[Box]]:
    # JEDEC 450 mil SOP-32: 11.3 x 20.5 mm body, ~14.2 mm lead span.
    leads: list[Box] = []
    for index in range(16):
        y = -9.525 + index * 1.27
        for side in (-1, 1):
            leads += [
                (side * 5.65, side * 6.25, y - 0.21, y + 0.21, 1.05, 1.25),
                (side * 6.10, side * 6.30, y - 0.21, y + 0.21, 0.0, 1.25),
                (side * 6.10, side * 7.10, y - 0.21, y + 0.21, 0.0, 0.15),
            ]
    return {
        "body": [(-5.65, 5.65, -10.25, 10.25, 0.15, 2.75)],
        "lead": leads,
        "mark": [(-4.6, -3.8, -9.2, -8.4, 2.75, 2.80)],
    }


def usb_c_model() -> dict[str, list[Box]]:
    # HRO TYPE-C-31-M-12 envelope; the mouth faces +y (board edge).
    pads = (-3.25, -2.45, -1.75, -1.25, -0.75, -0.25,
            0.25, 0.75, 1.25, 1.75, 2.45, 3.25)
    return {
        "shell": [(-4.47, 4.47, -3.65, 3.65, 0.0, 3.2)]
        + [(x - 0.3, x + 0.3, y - 0.4, y + 0.4, -1.4, 0.0)
           for x in (-4.32, 4.32) for y in (-3.13, 1.05)],
        "body": [(-4.1, 4.1, 3.4, 3.67, 0.35, 2.85)],
        "gold": [(-3.9, 3.9, 3.0, 3.68, 1.45, 1.75)]
        + [(x - 0.12, x + 0.12, -4.6, -3.65, 0.0, 0.12) for x in pads],
    }


OLED_SOCKET_HEIGHT = 5.0   # low-profile 1x4 female header body
OLED_SPACER_HEIGHT = 2.5   # module's own male-header plastic spacer


def oled_model() -> dict[str, list[Box]]:
    # Module board spans the footprint's F.Fab outline above the socket.
    top = OLED_SOCKET_HEIGHT + OLED_SPACER_HEIGHT
    return {
        "body": [(-1.27, 1.27, -8.89, 1.27, 0.0, OLED_SOCKET_HEIGHT),
                 (-1.27, 1.27, -8.89, 1.27, OLED_SOCKET_HEIGHT, top)],
        "gold": [(-0.32, 0.32, -0.32 - pitch, 0.32 - pitch, top, top + 1.6)
                 for pitch in (0.0, 2.54, 5.08, 7.62)],
        "pcb": [(-1.5, 36.5, -9.81, 2.19, top, top + 1.2)],
        "glass": [(3.0, 34.0, -9.3, 1.7, top + 1.2, top + 2.6)],
        "active": [(5.62, 28.0, -7.38, -1.8, top + 2.6, top + 2.62)],
    }


GENERATED_MODELS = {
    "SOP-32_11.305x20.495mm_P1.27mm": sop32_model,
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12": usb_c_model,
    "OLED_SSD1315_38x12mm_Socket": oled_model,
}


def model_path(footprint_name: str) -> str:
    return f"${{KIPRJMOD}}/3dmodels/{footprint_name}.wrl"


def vrml_text(parts: dict[str, list[Box]]) -> str:
    # KiCad VRML units are 0.1 inch with +Y opposite to footprint +y.
    lines = ["#VRML V2.0 utf8", "# Generated by scripts/build-kicad-smt-pcb.py"]
    faces = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    for colour, boxes in parts.items():
        points: list[str] = []
        indices: list[str] = []
        for x0, x1, y0, y1, z0, z1 in boxes:
            base = len(points)
            for z in (z0, z1):
                for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                    points.append(
                        f"{x / 2.54:.5f} {-y / 2.54:.5f} {z / 2.54:.5f}"
                    )
            indices += [
                " ".join(str(base + i) for i in face) + " -1"
                for face in faces
            ]
        red, green, blue = MODEL_COLOURS[colour]
        lines += [
            "Shape {",
            "  appearance Appearance { material Material {",
            f"    diffuseColor {red} {green} {blue} specularColor 0.3 0.3 0.3"
            " shininess 0.4 } }",
            "  geometry IndexedFaceSet { solid FALSE",
            "    coord Coordinate { point [ " + ", ".join(points) + " ] }",
            "    coordIndex [ " + " ".join(indices) + " ] }",
            "}",
        ]
    return "\n".join(lines) + "\n"


def write_3d_models() -> None:
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    for name, builder in GENERATED_MODELS.items():
        (MODEL_DIR / f"{name}.wrl").write_text(
            vrml_text(builder()), encoding="utf-8"
        )


def assign_3d_model(footprint: pcbnew.FOOTPRINT) -> None:
    name = str(footprint.GetFPID().GetLibItemName())
    if name not in GENERATED_MODELS:
        return
    footprint.Models().clear()
    model = pcbnew.FP_3DMODEL()
    model.m_Filename = model_path(name)
    footprint.Models().push_back(model)


def check_3d_models(board: pcbnew.BOARD) -> None:
    for name, builder in GENERATED_MODELS.items():
        path = MODEL_DIR / f"{name}.wrl"
        if not path.is_file() or \
                path.read_text(encoding="utf-8") != vrml_text(builder()):
            raise SystemExit(f"3D model {path.name} is missing or stale")
    for footprint in board.GetFootprints():
        name = str(footprint.GetFPID().GetLibItemName())
        files = [str(model.m_Filename) for model in footprint.Models()]
        if name in GENERATED_MODELS:
            if files != [model_path(name)]:
                raise SystemExit(f"{footprint.GetReference()} 3D model is stale")
            continue
        if not STOCK_MODEL_DIR.is_dir():
            continue
        for filename in files:
            resolved = Path(filename.replace(
                "${KICAD10_3DMODEL_DIR}", str(STOCK_MODEL_DIR)
            ))
            if not resolved.is_file():
                raise SystemExit(
                    f"{footprint.GetReference()} 3D model {filename} is missing"
                )


def load_footprint(root: Path, library_id: str) -> pcbnew.FOOTPRINT:
    library, name = library_id.split(":", 1)
    library_root = KICAD_DIR if library == "Z80SBC" else root
    footprint = pcbnew.FootprintLoad(str(library_root / f"{library}.pretty"), name)
    if footprint is None:
        raise SystemExit(f"cannot load footprint {library_id}")
    return footprint


def prepare_pico_footprint(footprint: pcbnew.FOOTPRINT, root: Path) -> None:
    # The SMD library footprint has no USB cable envelope; reuse the THT
    # footprint's, shifted from its pin-1 origin to the module centre. Load
    # it before removing zones: SWIG frees removed items on collection,
    # which corrupts the next library load, so keep them referenced.
    tht = load_footprint(root, "Module:RaspberryPi_Pico_Common_THT")
    usb = [zone for zone in tht.Zones() if str(zone.GetZoneName()) == "USB Cable"]
    if len(usb) != 1:
        raise SystemExit("THT Pico footprint has no USB cable envelope")
    for zone in list(footprint.Zones()):
        if str(zone.GetZoneName()) in PICO_DROPPED_ZONES:
            footprint.Remove(zone)
            _DETACHED_ITEMS.append(zone)
    envelope = pcbnew.ZONE(usb[0])
    envelope.Move(vec(-PICO_PIN1_OFFSET[0], -PICO_PIN1_OFFSET[1]))
    footprint.Add(envelope)
    _DETACHED_ITEMS.append(tht)


def merge_stacked_pad_openings(footprint: pcbnew.FOOTPRINT) -> None:
    # USB-C pairs such as A1/B12 are identical pads at one position. Keep
    # both for connectivity, but emit one mask and paste opening so JLCDFM
    # does not report a multi-segment solder mask opening.
    seen = set()
    for pad in footprint.Pads():
        key = (
            pad.GetPosition().x, pad.GetPosition().y,
            pad.GetSize(pcbnew.F_Cu).x, pad.GetSize(pcbnew.F_Cu).y,
            pad.GetShape(pcbnew.F_Cu), pad.GetOrientationDegrees(),
            pad.GetAttribute(),
        )
        if key not in seen:
            seen.add(key)
            continue
        layers = pad.GetLayerSet()
        for layer in (pcbnew.F_Mask, pcbnew.F_Paste, pcbnew.B_Mask, pcbnew.B_Paste):
            layers.RemoveLayer(layer)
        pad.SetLayerSet(layers)


def rel(x: float, y: float, rotation: float = 0) -> tuple[float, float, float]:
    return (round(BOARD_LEFT + x, 3), round(BOARD_TOP + y, 3), rotation)


def pico_pin_y(pin: int) -> float:
    # Board-relative y of a Pico pad (A1 rotated 180 degrees).
    row = pin - 1 if pin <= 20 else 40 - pin
    return round(PICO_Y - BOARD_TOP - row * 2.54, 3)


# J2/J3 hug the left edge to keep their plated pads > 3.05 mm from the
# Pico castellations (JLCDFM THT-to-SMD).
HEADER_X = 2.85
CHANNEL_X = 32.1        # Pico-side pull resistors, between Pico and buffers
BUFFER_X = 40.8         # SOIC-20W buffer column
BYPASS_X = 47.65        # decoupling column, > 3.05 mm from U1 THT pads
Z80_X = 52.2            # U1 pin 1
Z80_Y = 3.0
SRAM_X = 79.2
STRIP_X = 94.5          # right-edge electrolytic column

# Pico-side pull resistors sit level with the Pico pad they bias so each
# stub is a short F.Cu spur. R23/R24 (D6/D7 on the far Pico row) use the
# free GND-pad slots.
CHANNEL_PULLS = {
    "R37": 4, "R38": 5, "R33": 6, "R34": 7, "R35": 9, "R36": 10,
    "R40": 11, "R39": 12, "R17": 14, "R18": 15, "R19": 16, "R20": 17,
    "R21": 19, "R22": 20, "R23": 18, "R24": 13,
}
# Z80-side 5 V pull-ups: a grid in the top-right corner, above U3. The SRAM
# sits below U3, rotated so WE/CE face the AHCT244 at the foot of the
# buffer column.
GRID_PULLS = [f"R{index}" for index in range(1, 17)] + [
    f"R{index}" for index in range(25, 33)
]


def placement_table() -> dict[str, tuple[float, float, float]]:
    table = {
        "A1": (round(PICO_X - PICO_PIN1_OFFSET[0], 3),
               round(PICO_Y - PICO_PIN1_OFFSET[1], 3), 180),
        "J3": rel(HEADER_X, 13.0),
        "J2": rel(HEADER_X, pico_pin_y(29)),
        "D1": rel(3.2, 47.0, 90),
        "U9": rel(BUFFER_X, 7.7),
        "U10": rel(BUFFER_X, 21.3),
        "U7": rel(BUFFER_X, 34.9),
        "U4": rel(BUFFER_X, 48.31),
        "C6": rel(BYPASS_X, 3.3, 90),
        "C7": rel(BYPASS_X, 16.9, 270),
        "C1": rel(BYPASS_X, 24.9, 90),
        "C5": rel(BYPASS_X, 29.7, 90),
        "C4": rel(BYPASS_X, 40.81, 90),
        "U1": rel(Z80_X, Z80_Y),
        "U2": rel(SRAM_X, 43.3, 180),
        "C2": rel(73.3, 56.6),
        "U3": rel(77.3, 19.3),
        "C3": rel(82.3, 18.6, 270),
        "C8": rel(STRIP_X, 11.1, 90),
        "C10": rel(STRIP_X, 20.95, 90),
        "J1": rel(BOARD_WIDTH - USB_C_MOUTH_OFFSET, 33.8, 90),
        # CC pull-downs behind the receptacle, > 3.05 mm from its shell
        # legs; R41 sits between the legs, beside the SRAM courtyard.
        "R41": rel(87.8, 33.8, 90),
        "R42": rel(86.6, 30.7, 90),
        "C11": rel(STRIP_X, 46.65, 90),
        "C9": rel(87.1, 19.5, 90),
        # OLED module body spans the low-profile SOIC buffer column.
        "A2": rel(BUFFER_X + 3.8, 58.35, 90),
    }
    for reference, pin in CHANNEL_PULLS.items():
        table[reference] = rel(CHANNEL_X, pico_pin_y(pin))
    for index, reference in enumerate(GRID_PULLS):
        column, row = divmod(index, 5)
        table[reference] = rel(74.6 + column * 3.6, 4.2 + row * 2.2)
    return table


CHIP_PASSIVES = {f"R{index}" for index in range(1, 43)} | {
    f"C{index}" for index in range(1, 8)
}
TITLE_FRONT_POSITION = rel(58.0, 57.6)[:2]
# Pin-1 dot offset from pad 1's outer edge and centre line; U3, U7 and U10 sit
# above the pad where routed vias leave room.
PIN1_DOT_OFFSETS = {
    "U3": (0.0, -0.85), "U7": (0.0, -1.0), "U10": (-0.4, -0.8),
}
# Back silkscreen behind the Pico body, below the antenna notch, where the
# routed board leaves no vias or plated holes.
TITLE_BACK_POSITION = rel(19.0, 38.5)[:2]


def mounting_hole_table() -> dict[str, tuple[float, float]]:
    return {
        "H1": rel(3.2, 3.2)[:2],
        "H2": rel(BOARD_WIDTH - 3.2, 3.2)[:2],
        "H3": rel(BOARD_WIDTH - 3.2, BOARD_HEIGHT - 3.2)[:2],
        "H4": rel(3.2, BOARD_HEIGHT - 3.2)[:2],
    }


# Vertical pin labels between the 2.54 mm headers and the Pico castellations.
HEADER_PIN_LABELS = tuple(
    (text, rel(5.55, pico_pin_y(29) + index * 2.54)[0],
     rel(5.55, pico_pin_y(29) + index * 2.54)[1])
    for index, text in enumerate(("SDA", "SCL", "3V3", "GND"))
) + tuple(
    (text, *rel(5.0, 13.0 + index * 2.54)[:2])
    for index, text in enumerate(("+5V", "GND"))
)


def assembly_labels() -> list[tuple[str, float, float, float]]:
    return [
        (text, *rel(x, y)[:2], size) for text, x, y, size in (
            ("A1 PICO 2 W", 18.1, 32.0, 1.0),
            ("USB-C OR J3\nONE SUPPLY", 93.3, 53.6, 0.8),
            ("OLED", 31.5, 58.6, 0.8),
        )
    ]


IC_LABELS = {
    "U1": "Z80 CPU",
    "U2": "IS62C1024 SRAM",
    "U3": "74HCT32",
    "U4": "74AHCT244",
    "U7": "74LVC244",
    "U9": "74AHCT245",
    "U10": "74LVC245",
}


def ic_label_text(reference: str) -> str:
    return f"{IC_LABELS[reference]}\n{reference}"


REFERENCE_POSITIONS: dict[str, tuple[float, float]] = {}

# The Z80 label sits in the via-free stretch between the DIP rows.
IC_LABEL_OFFSETS: dict[str, tuple[float, float]] = {"U1": (0.5, 7.75)}



def add_segment(
    board: pcbnew.BOARD,
    layer: int,
    start: tuple[float, float],
    end: tuple[float, float],
    width: float = 0.25,
) -> None:
    item = pcbnew.PCB_SHAPE(board)
    item.SetShape(pcbnew.SHAPE_T_SEGMENT)
    item.SetLayer(layer)
    item.SetStart(vec(*start))
    item.SetEnd(vec(*end))
    item.SetWidth(mm(width))
    board.Add(item)


def outline_paths() -> list[list[tuple[float, float]]]:
    # Rectangular board plus an enclosed, chamfered antenna window.
    chamfer = ANTENNA_CORNER_CHAMFER
    return [[
        (BOARD_LEFT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_TOP),
    ], [
        (ANTENNA_LEFT + chamfer, ANTENNA_TOP),
        (ANTENNA_RIGHT - chamfer, ANTENNA_TOP),
        (ANTENNA_RIGHT, ANTENNA_TOP + chamfer),
        (ANTENNA_RIGHT, ANTENNA_BOTTOM - chamfer),
        (ANTENNA_RIGHT - chamfer, ANTENNA_BOTTOM),
        (ANTENNA_LEFT + chamfer, ANTENNA_BOTTOM),
        (ANTENNA_LEFT, ANTENNA_BOTTOM - chamfer),
        (ANTENNA_LEFT, ANTENNA_TOP + chamfer),
        (ANTENNA_LEFT + chamfer, ANTENNA_TOP),
    ]]


def add_outline(board: pcbnew.BOARD) -> None:
    for path in outline_paths():
        for start, end in zip(path, path[1:]):
            add_segment(board, pcbnew.Edge_Cuts, start, end, 0.25)


def add_text(
    board: pcbnew.BOARD,
    text: str,
    x: float,
    y: float,
    size: float,
    angle: float = 0,
    layer: int = pcbnew.F_SilkS,
) -> None:
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetLayer(layer)
    item.SetMirrored(layer == pcbnew.B_SilkS)
    item.SetPosition(vec(x, y))
    item.SetTextSize(vec(size, size))
    item.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    item.SetTextThickness(mm(min(0.25, max(SILK_MIN_STROKE, size * 0.12))))
    board.Add(item)


def silkscreen_items(board: pcbnew.BOARD) -> list[pcbnew.BOARD_ITEM]:
    items = list(board.GetDrawings())
    for footprint in board.GetFootprints():
        items.extend(footprint.GraphicalItems())
        items.extend(footprint.GetFields())
    return [
        item for item in items
        if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS)
        and (
            item.Type() not in (pcbnew.PCB_TEXT_T, pcbnew.PCB_FIELD_T)
            or item.IsVisible()
        )
    ]


def silkscreen_holes(
    board: pcbnew.BOARD,
) -> list[tuple[str, pcbnew.SHAPE_SEGMENT]]:
    holes = [
        (f"{footprint.GetReference()}.{pad.GetNumber()}",
         pad.GetEffectiveHoleShape())
        for footprint in board.GetFootprints()
        for pad in footprint.Pads()
        if pad.GetDrillSize().x > 0
    ]
    for track in board.GetTracks():
        if track.Type() == pcbnew.PCB_VIA_T:
            via = pcbnew.Cast_to_PCB_VIA(track)
            position = via.GetPosition()
            # Imported vias can inherit their drill; the raw hole shape uses -1.
            drill = via.GetDrillValue()
            if drill <= 0:
                raise SystemExit("via has no resolved drill diameter")
            holes.append((
                f"via at {pcbnew.ToMM(position.x):.3f},"
                f"{pcbnew.ToMM(position.y):.3f}",
                pcbnew.SHAPE_SEGMENT(position, position, drill),
            ))
    return holes


def silkscreen_pads(
    board: pcbnew.BOARD,
) -> list[tuple[str, int, pcbnew.SHAPE, int]]:
    """Return (name, silk layer, copper shape, mask expansion) per pad side."""
    pads = []
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            for silk, copper, mask in (
                (pcbnew.F_SilkS, pcbnew.F_Cu, pcbnew.F_Mask),
                (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Mask),
            ):
                if not pad.IsOnLayer(copper):
                    continue
                expansion = max(0, pad.GetSolderMaskExpansion(mask)) \
                    if pad.IsOnLayer(mask) else 0
                pads.append((
                    f"{footprint.GetReference()}.{pad.GetNumber()}",
                    silk, pad.GetEffectiveShape(copper), expansion,
                ))
    return pads


def silkscreen_obstacles(
    board: pcbnew.BOARD, layer: int,
) -> list[tuple[str, pcbnew.SHAPE, int]]:
    """Return (name, shape, required clearance) for one silkscreen layer."""
    obstacles = [
        (name, hole, mm(SILK_HOLE_CLEARANCE))
        for name, hole in silkscreen_holes(board)
    ]
    obstacles.extend(
        (f"pad {name}", shape, mm(SILK_PAD_CLEARANCE) + expansion)
        for name, silk, shape, expansion in silkscreen_pads(board)
        if silk == layer
    )
    return obstacles


def gap_silkscreen_lines(board: pcbnew.BOARD) -> None:
    obstacles = {
        layer: silkscreen_obstacles(board, layer)
        for layer in (pcbnew.F_SilkS, pcbnew.B_SilkS)
    }
    step = mm(0.005)
    for item in silkscreen_items(board):
        if item.Type() != pcbnew.PCB_SHAPE_T or \
                item.GetShape() != pcbnew.SHAPE_T_SEGMENT:
            continue
        start, end = item.GetStart(), item.GetEnd()
        dx, dy = end.x - start.x, end.y - start.y
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        shape = item.GetEffectiveShape()
        nearby = [
            (obstacle, clearance + mm(0.01))
            for _, obstacle, clearance in obstacles[item.GetLayer()]
            if shape.Collide(obstacle, clearance)
        ]
        if not nearby:
            continue
        # Sample the stroke centreline so any pad or drill shape can be gapped.
        samples = max(2, math.ceil(length / step) + 1)
        blocked = []
        for index in range(samples):
            t = index / (samples - 1)
            point = pcbnew.VECTOR2I(
                round(start.x + t * dx), round(start.y + t * dy)
            )
            dot = pcbnew.SHAPE_SEGMENT(point, point, item.GetWidth())
            blocked.append(any(
                dot.Collide(obstacle, clearance)
                for obstacle, clearance in nearby
            ))
        pad_t = 1 / (samples - 1)
        gaps = []
        index = 0
        while index < samples:
            if not blocked[index]:
                index += 1
                continue
            first = index
            while index < samples and blocked[index]:
                index += 1
            gaps.append((
                max(0.0, (first - 1) * pad_t),
                min(1.0, index * pad_t),
            ))
        spans = []
        cursor = 0.0
        for left, right in sorted(gaps):
            if left > cursor:
                spans.append((cursor, left))
            cursor = max(cursor, right)
        if cursor < 1.0:
            spans.append((cursor, 1.0))
        owner = item.GetParentFootprint() or board
        for left, right in spans:
            if (right - left) * length < mm(0.05):
                continue
            segment = pcbnew.PCB_SHAPE(owner)
            segment.SetShape(pcbnew.SHAPE_T_SEGMENT)
            segment.SetLayer(item.GetLayer())
            segment.SetWidth(item.GetWidth())
            segment.SetStart(pcbnew.VECTOR2I(
                round(start.x + left * dx), round(start.y + left * dy)
            ))
            segment.SetEnd(pcbnew.VECTOR2I(
                round(start.x + right * dx), round(start.y + right * dy)
            ))
            owner.Add(segment)
        owner.Remove(item)


def check_silkscreen(board: pcbnew.BOARD) -> None:
    obstacles = {
        layer: silkscreen_obstacles(board, layer)
        for layer in (pcbnew.F_SilkS, pcbnew.B_SilkS)
    }
    violations = []
    items = silkscreen_items(board)
    for item in items:
        position = item.GetPosition()
        description = (
            f"{board.GetLayerName(item.GetLayer())} at "
            f"{pcbnew.ToMM(position.x):.3f},{pcbnew.ToMM(position.y):.3f}"
        )
        if item.Type() in (pcbnew.PCB_TEXT_T, pcbnew.PCB_FIELD_T):
            description += f" text {item.GetText()!r}"
            width = item.GetTextThickness()
            shape = item.GetEffectiveTextShape()
        elif item.Type() == pcbnew.PCB_SHAPE_T:
            width = item.GetWidth()
            # Filled circles report only their radius as the effective
            # shape; the printed dot also includes half the stroke width.
            shape = pcbnew.SHAPE_POLY_SET()
            item.TransformShapeToPolygon(
                shape, item.GetLayer(), 0, mm(0.002), pcbnew.ERROR_OUTSIDE,
            )
        else:
            raise SystemExit(f"unsupported silkscreen item: {description}")
        if width < mm(SILK_MIN_STROKE):
            violations.append(
                f"{description}: stroke {pcbnew.ToMM(width):.3f} mm "
                f"is below {SILK_MIN_STROKE:.2f} mm"
            )
        for name, obstacle, clearance in obstacles[item.GetLayer()]:
            if shape.Collide(obstacle, clearance):
                violations.append(
                    f"{description}: less than "
                    f"{pcbnew.ToMM(clearance):.2f} mm from {name}"
                )
    if violations:
        raise SystemExit("Silkscreen DFM failed:\n" + "\n".join(violations))
    print(
        f"PASS: {len(items)} silkscreen items have >= "
        f"{SILK_MIN_STROKE:.2f} mm strokes, >= "
        f"{SILK_HOLE_CLEARANCE:.2f} mm drill clearance and >= "
        f"{SILK_PAD_CLEARANCE:.2f} mm same-side pad clearance"
    )


# JLCDFM "solder mask opening exposing trace": danger < 0.08 mm, warning
# 0.08-0.15 mm, good > 0.15 mm. The autorouter's 45-degree pad escapes leave
# the following run skimming its own pad's opening, so those exits are
# straightened until every track clears every mask opening it does not
# connect to by this target. Other-net tracks at the 0.20 mm copper rule sit
# about 0.16 mm from a 0.05 mm-expanded opening.
MASK_TRACK_CLEARANCE = 0.155
# JLCDFM warns about 1:1 (zero-expansion) mask openings. The 0.5 mm-pitch
# USB-C pads use less so their mask webs stay >= 0.12 mm.
SOLDER_MASK_EXPANSION = 0.05
USB_C_MASK_EXPANSION = 0.04
MASK_TRACK_STEP = 0.01
ROUTE_MARGIN = 0.005
MASK_LAYERS = ((pcbnew.F_Cu, pcbnew.F_Mask), (pcbnew.B_Cu, pcbnew.B_Mask))


def uid(item: pcbnew.BOARD_ITEM) -> str:
    return item.m_Uuid.AsString()


def mask_opening(pad: pcbnew.PAD, mask_layer: int) -> pcbnew.SHAPE_POLY_SET:
    poly = pcbnew.SHAPE_POLY_SET()
    pad.TransformShapeToPolygon(
        poly, mask_layer, pad.GetSolderMaskExpansion(mask_layer),
        mm(0.002), pcbnew.ERROR_OUTSIDE,
    )
    return poly


def poly_segment_gap(
    poly: pcbnew.SHAPE_POLY_SET,
    start: pcbnew.VECTOR2I,
    end: pcbnew.VECTOR2I,
    width: int,
) -> float:
    distance = math.sqrt(poly.SquaredDistanceToSeg(pcbnew.SEG(start, end)))
    return pcbnew.ToMM(distance - width / 2)


def mask_openings(
    board: pcbnew.BOARD, copper_layer: int, mask_layer: int
) -> list[tuple[pcbnew.PAD, pcbnew.SHAPE_POLY_SET, pcbnew.SHAPE_POLY_SET]]:
    openings = []
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if not pad.IsOnLayer(mask_layer):
                continue
            copper = pcbnew.SHAPE_POLY_SET()
            if pad.IsOnLayer(copper_layer):
                pad.TransformShapeToPolygon(
                    copper, copper_layer, 0, mm(0.002), pcbnew.ERROR_INSIDE,
                )
            openings.append((pad, mask_opening(pad, mask_layer), copper))
    return openings


def mask_track_violations(
    board: pcbnew.BOARD, copper_layer: int, mask_layer: int
) -> list[tuple[float, pcbnew.PAD, pcbnew.PCB_TRACK]]:
    openings = mask_openings(board, copper_layer, mask_layer)
    violations = []
    for track in board.GetTracks():
        if track.Type() != pcbnew.PCB_TRACE_T or track.GetLayer() != copper_layer:
            continue
        box = track.GetBoundingBox()
        box.Inflate(mm(MASK_TRACK_CLEARANCE + 1.0))
        for pad, opening, copper in openings:
            if not box.Intersects(pad.GetBoundingBox()):
                continue
            gap = poly_segment_gap(
                opening, track.GetStart(), track.GetEnd(), track.GetWidth(),
            )
            if gap >= MASK_TRACK_CLEARANCE:
                continue
            if copper.OutlineCount() and poly_segment_gap(
                copper, track.GetStart(), track.GetEnd(), track.GetWidth(),
            ) <= 0:
                continue
            violations.append((gap, pad, track))
    return sorted(violations, key=lambda item: item[0])


def same_net_chain(
    board: pcbnew.BOARD,
    pad: pcbnew.PAD,
    target: pcbnew.PCB_TRACK,
    copper_layer: int,
) -> list[pcbnew.PCB_TRACK] | None:
    # Find the unbranched run of tracks from the pad to the offending track.
    tracks = [
        track for track in board.GetTracks()
        if track.Type() == pcbnew.PCB_TRACE_T
        and track.GetLayer() == copper_layer
        and track.GetNetCode() == target.GetNetCode()
    ]
    vias = {
        (via.GetPosition().x, via.GetPosition().y)
        for via in board.GetTracks() if via.Type() == pcbnew.PCB_VIA_T
    }
    ends: dict[tuple[int, int], list[pcbnew.PCB_TRACK]] = {}
    for track in tracks:
        for point in (track.GetStart(), track.GetEnd()):
            ends.setdefault((point.x, point.y), []).append(track)
    copper = pcbnew.SHAPE_POLY_SET()
    pad.TransformShapeToPolygon(
        copper, copper_layer, 0, mm(0.002), pcbnew.ERROR_INSIDE,
    )
    for first in tracks:
        for anchor, other in (
            (first.GetStart(), first.GetEnd()), (first.GetEnd(), first.GetStart()),
        ):
            if not copper.Contains(anchor):
                continue
            chain = [first]
            point = other
            while uid(chain[-1]) != uid(target):
                key = (point.x, point.y)
                following = [
                    t for t in ends.get(key, []) if uid(t) != uid(chain[-1])
                ]
                if key in vias or len(following) != 1:
                    break
                track = following[0]
                chain.append(track)
                point = (
                    track.GetEnd() if track.GetStart() == point
                    else track.GetStart()
                )
            if uid(chain[-1]) == uid(target):
                return chain
    return None


def chain_points(
    chain: list[pcbnew.PCB_TRACK], copper: pcbnew.SHAPE_POLY_SET
) -> list[pcbnew.VECTOR2I]:
    first = chain[0]
    if copper.Contains(first.GetStart()):
        points = [first.GetStart(), first.GetEnd()]
    else:
        points = [first.GetEnd(), first.GetStart()]
    for track in chain[1:]:
        points.append(
            track.GetEnd() if track.GetStart() == points[-1]
            else track.GetStart()
        )
    return points


def segment_clear_of_other_nets(
    board: pcbnew.BOARD,
    start: pcbnew.VECTOR2I,
    end: pcbnew.VECTOR2I,
    width: int,
    net_code: int,
    copper_layer: int,
    mask_layer: int,
    own_pad: pcbnew.PAD,
    ignored: list[pcbnew.PCB_TRACK],
) -> bool:
    segment = pcbnew.SEG(start, end)
    box = pcbnew.BOX2I(start, end - start)
    box.Normalize()
    box.Inflate(mm(CLOCK_CLEARANCE + 2.0))
    net_name = board.FindNet(net_code).GetNetname() if net_code else ""
    ignored_ids = {uid(track) for track in ignored}
    for item in board.GetTracks():
        if uid(item) in ignored_ids or not box.Intersects(item.GetBoundingBox()):
            continue
        if item.Type() == pcbnew.PCB_VIA_T:
            same = item.GetNetCode() == net_code
            gap = math.sqrt(segment.SquaredDistance(item.GetPosition()))
            if not same and gap - item.GetWidth(copper_layer) / 2 - width / 2 < mm(
                SIGNAL_CLEARANCE + ROUTE_MARGIN
            ):
                return False
            continue
        if item.GetLayer() != copper_layer or item.GetNetCode() == net_code:
            continue
        other = pcbnew.SEG(item.GetStart(), item.GetEnd())
        clearance = (
            CLOCK_CLEARANCE
            if {net_name, str(item.GetNetname())} & {"PICO_CLK", "Z80_CLK"}
            else SIGNAL_CLEARANCE
        )
        if segment.Distance(other) - (item.GetWidth() + width) / 2 < mm(
            clearance + ROUTE_MARGIN
        ):
            return False
    for footprint in board.GetFootprints():
        if not box.Intersects(footprint.GetBoundingBox()):
            continue
        for pad in footprint.Pads():
            if uid(pad) == uid(own_pad):
                continue
            if pad.IsOnLayer(copper_layer) and pad.GetNetCode() != net_code:
                copper = pcbnew.SHAPE_POLY_SET()
                pad.TransformShapeToPolygon(
                    copper, copper_layer, 0, mm(0.002), pcbnew.ERROR_OUTSIDE,
                )
                if poly_segment_gap(copper, start, end, width) < (
                    SIGNAL_CLEARANCE + ROUTE_MARGIN
                ):
                    return False
            if pad.IsOnLayer(mask_layer):
                connected = pcbnew.SHAPE_POLY_SET()
                if pad.IsOnLayer(copper_layer):
                    pad.TransformShapeToPolygon(
                        connected, copper_layer, 0, mm(0.002),
                        pcbnew.ERROR_INSIDE,
                    )
                if connected.OutlineCount() and poly_segment_gap(
                    connected, start, end, width,
                ) <= 0:
                    continue
                if poly_segment_gap(
                    mask_opening(pad, mask_layer), start, end, width,
                ) < MASK_TRACK_CLEARANCE:
                    return False
    return True


def straighten_pad_exit(
    board: pcbnew.BOARD,
    pad: pcbnew.PAD,
    target: pcbnew.PCB_TRACK,
    copper_layer: int,
    mask_layer: int,
) -> bool:
    chain = same_net_chain(board, pad, target, copper_layer)
    if chain is None:
        return False
    copper = pcbnew.SHAPE_POLY_SET()
    pad.TransformShapeToPolygon(
        copper, copper_layer, 0, mm(0.002), pcbnew.ERROR_INSIDE,
    )
    opening = mask_opening(pad, mask_layer)
    points = chain_points(chain, copper)
    width = chain[0].GetWidth()
    # Continue past the offending track while the run stays unbranched so
    # the new corner can land beyond the pad if needed.
    for index in range(1, len(points) - 1):
        segment_start, segment_end = points[index], points[index + 1]
        length = (segment_end - segment_start).EuclideanNorm()
        steps = max(1, int(length // mm(MASK_TRACK_STEP)))
        for step in range(steps):
            fraction = step / steps
            corner = pcbnew.VECTOR2I(
                round(segment_start.x + (segment_end.x - segment_start.x) * fraction),
                round(segment_start.y + (segment_end.y - segment_start.y) * fraction),
            )
            remainder = [corner] + points[index + 1:]
            if any(
                poly_segment_gap(opening, a, b, chain[k].GetWidth())
                < MASK_TRACK_CLEARANCE
                for k, (a, b) in enumerate(
                    zip(remainder, remainder[1:]), start=index,
                )
            ):
                continue
            if not segment_clear_of_other_nets(
                board, points[0], corner, width, target.GetNetCode(),
                copper_layer, mask_layer, pad, chain,
            ):
                continue
            locked = any(track.IsLocked() for track in chain[:index + 1])
            kept = chain[index]
            for track in chain[:index]:
                board.Remove(track)
            if kept.GetStart() == segment_start:
                kept.SetStart(corner)
            else:
                kept.SetEnd(corner)
            exit_track = pcbnew.PCB_TRACK(board)
            exit_track.SetStart(points[0])
            exit_track.SetEnd(corner)
            exit_track.SetLayer(copper_layer)
            exit_track.SetWidth(width)
            exit_track.SetNet(target.GetNet())
            exit_track.SetLocked(locked)
            board.Add(exit_track)
            return True
    return False


def slide_stub_junction(
    board: pcbnew.BOARD,
    pad: pcbnew.PAD,
    target: pcbnew.PCB_TRACK,
    copper_layer: int,
    mask_layer: int,
) -> bool:
    # A routed stub that tees into a locked same-net track beside the pad is
    # moved along that locked track until the stub clears the mask opening.
    if target.IsLocked():
        return False
    opening = mask_opening(pad, mask_layer)
    same_net = [
        track for track in board.GetTracks()
        if track.Type() == pcbnew.PCB_TRACE_T
        and track.GetLayer() == copper_layer
        and track.GetNetCode() == target.GetNetCode()
    ]
    locked = [track for track in same_net if track.IsLocked()]
    vias = {
        (via.GetPosition().x, via.GetPosition().y)
        for via in board.GetTracks() if via.Type() == pcbnew.PCB_VIA_T
    }

    def touching(point: pcbnew.VECTOR2I) -> list[pcbnew.PCB_TRACK]:
        return [t for t in same_net if point in (t.GetStart(), t.GetEnd())]

    for junction in (target.GetStart(), target.GetEnd()):
        hosts = [
            host for host in locked if junction in (host.GetStart(), host.GetEnd())
        ]
        if not hosts:
            continue
        # Unbranched run of unlocked tracks leading away from the junction.
        stub = [target]
        points = [
            junction,
            target.GetEnd() if target.GetStart() == junction else target.GetStart(),
        ]
        while (points[-1].x, points[-1].y) not in vias:
            following = [t for t in touching(points[-1]) if uid(t) != uid(stub[-1])]
            if len(following) != 1 or following[0].IsLocked():
                break
            stub.append(following[0])
            points.append(
                following[0].GetEnd() if following[0].GetStart() == points[-1]
                else following[0].GetStart()
            )
        width = target.GetWidth()
        for host in hosts:
            far = host.GetEnd() if host.GetStart() == junction else host.GetStart()
            length = (far - junction).EuclideanNorm()
            steps = max(1, int(length // mm(MASK_TRACK_STEP)))
            for anchor_index in range(1, len(points)):
                anchor = points[anchor_index]
                removed = stub[:anchor_index]
                if any(
                    poly_segment_gap(
                        opening, track.GetStart(), track.GetEnd(), track.GetWidth(),
                    ) < MASK_TRACK_CLEARANCE
                    for track in stub[anchor_index:]
                ):
                    continue
                for step in range(steps, 0, -1):
                    fraction = step / steps
                    point = pcbnew.VECTOR2I(
                        round(junction.x + (far.x - junction.x) * fraction),
                        round(junction.y + (far.y - junction.y) * fraction),
                    )
                    if poly_segment_gap(
                        opening, anchor, point, width,
                    ) < MASK_TRACK_CLEARANCE:
                        continue
                    if not segment_clear_of_other_nets(
                        board, anchor, point, width, target.GetNetCode(),
                        copper_layer, mask_layer, pad, removed,
                    ):
                        continue
                    if point != far:
                        tail = pcbnew.PCB_TRACK(board)
                        tail.SetStart(point)
                        tail.SetEnd(far)
                        tail.SetLayer(copper_layer)
                        tail.SetWidth(host.GetWidth())
                        tail.SetNet(host.GetNet())
                        tail.SetLocked(True)
                        board.Add(tail)
                        if host.GetStart() == far:
                            host.SetStart(point)
                        else:
                            host.SetEnd(point)
                    for track in removed[1:]:
                        board.Remove(track)
                    target.SetStart(anchor)
                    target.SetEnd(point)
                    return True
    return False


def straighten_pad_exits(board: pcbnew.BOARD) -> None:
    for copper_layer, mask_layer in MASK_LAYERS:
        for _ in range(200):
            violations = mask_track_violations(board, copper_layer, mask_layer)
            if not any(
                straighten_pad_exit(board, pad, track, copper_layer, mask_layer)
                or slide_stub_junction(
                    board, pad, track, copper_layer, mask_layer,
                )
                for _, pad, track in violations
                if pad.GetNetCode() == track.GetNetCode()
            ):
                break


# JLCDFM measures vias against rectangular pads as if their corners were
# square, so a via sitting diagonally off a rounded SOIC corner reports about
# 0.05 mm less than the true gap (0.12 mm for a via 0.17 mm from the real
# opening). Vias are kept this far from each square-cornered mask opening.
VIA_MASK_CLEARANCE = 0.16
VIA_NUDGE_ANGLES = (0, 30, -30, 60, -60, 75, -75, 90, -90)
SQUARE_PAD_SHAPES = {
    pcbnew.PAD_SHAPE_RECTANGLE, pcbnew.PAD_SHAPE_ROUNDRECT,
    pcbnew.PAD_SHAPE_CHAMFERED_RECT,
}


def via_mask_gap(
    pad: pcbnew.PAD, mask_layer: int, centre: pcbnew.VECTOR2I, radius: int,
) -> tuple[float, float, float]:
    """Return the gap in mm and the unit push direction away from the pad."""
    if pad.GetShape(pcbnew.F_Cu) in SQUARE_PAD_SHAPES:
        size = pad.GetSize(pcbnew.F_Cu)
        expansion = pad.GetSolderMaskExpansion(mask_layer)
        half_x, half_y = size.x / 2 + expansion, size.y / 2 + expansion
        angle = math.radians(pad.GetOrientation().AsDegrees())
        cos, sin = math.cos(angle), math.sin(angle)
        dx = centre.x - pad.GetPosition().x
        dy = centre.y - pad.GetPosition().y
        local_x, local_y = dx * cos - dy * sin, dx * sin + dy * cos
        out_x = math.copysign(max(abs(local_x) - half_x, 0), local_x)
        out_y = math.copysign(max(abs(local_y) - half_y, 0), local_y)
        distance = math.hypot(out_x, out_y)
        if distance:
            push_x = (out_x * cos + out_y * sin) / distance
            push_y = (-out_x * sin + out_y * cos) / distance
        else:
            push_x, push_y = 0.0, 0.0
        return pcbnew.ToMM(distance - radius), push_x, push_y
    opening = mask_opening(pad, mask_layer)
    distance = math.sqrt(opening.SquaredDistance(centre))
    dx = centre.x - pad.GetPosition().x
    dy = centre.y - pad.GetPosition().y
    norm = math.hypot(dx, dy) or 1.0
    return pcbnew.ToMM(distance - radius), dx / norm, dy / norm


def via_mask_violations(
    board: pcbnew.BOARD,
) -> list[tuple[float, pcbnew.PAD, pcbnew.PCB_VIA, float, float]]:
    violations = []
    pads = [pad for footprint in board.GetFootprints() for pad in footprint.Pads()]
    for via in board.GetTracks():
        if via.Type() != pcbnew.PCB_VIA_T:
            continue
        for copper_layer, mask_layer in MASK_LAYERS:
            radius = via.GetWidth(copper_layer) // 2
            for pad in pads:
                if pad.GetNetCode() == via.GetNetCode() or not pad.IsOnLayer(mask_layer):
                    continue
                if (pad.GetPosition() - via.GetPosition()).EuclideanNorm() > mm(6):
                    continue
                gap, push_x, push_y = via_mask_gap(
                    pad, mask_layer, via.GetPosition(), radius,
                )
                if gap < VIA_MASK_CLEARANCE:
                    violations.append((gap, pad, via, push_x, push_y))
    return sorted(violations, key=lambda item: item[0])


def via_position_clear(
    board: pcbnew.BOARD, via: pcbnew.PCB_VIA, centre: pcbnew.VECTOR2I,
) -> bool:
    radius = via.GetWidth(pcbnew.F_Cu) // 2
    limit = mm(SIGNAL_CLEARANCE + ROUTE_MARGIN)
    for item in board.GetTracks():
        if item.GetNetCode() == via.GetNetCode() or uid(item) == uid(via):
            continue
        if item.Type() == pcbnew.PCB_VIA_T:
            other_radius = item.GetWidth(pcbnew.F_Cu) // 2
            if (item.GetPosition() - centre).EuclideanNorm() - radius - other_radius < limit:
                return False
        elif item.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu):
            gap = pcbnew.SEG(item.GetStart(), item.GetEnd()).Distance(centre)
            if gap - radius - item.GetWidth() // 2 < limit:
                return False
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if (pad.GetPosition() - centre).EuclideanNorm() > mm(6):
                continue
            for copper_layer, mask_layer in MASK_LAYERS:
                if pad.GetNetCode() != via.GetNetCode() and pad.IsOnLayer(copper_layer):
                    copper = pcbnew.SHAPE_POLY_SET()
                    pad.TransformShapeToPolygon(
                        copper, copper_layer, 0, mm(0.002), pcbnew.ERROR_OUTSIDE,
                    )
                    if math.sqrt(copper.SquaredDistance(centre)) - radius < limit:
                        return False
                if (
                    pad.GetNetCode() != via.GetNetCode()
                    and pad.IsOnLayer(mask_layer)
                    and via_mask_gap(pad, mask_layer, centre, radius)[0]
                    < VIA_MASK_CLEARANCE
                ):
                    return False
    return True


def nudge_via(
    board: pcbnew.BOARD, via: pcbnew.PCB_VIA, gap: float,
    push_x: float, push_y: float,
) -> bool:
    origin = pcbnew.VECTOR2I(via.GetPosition())
    attached = [
        track for track in board.GetTracks()
        if track.Type() == pcbnew.PCB_TRACE_T
        and track.GetNetCode() == via.GetNetCode()
        and origin in (track.GetStart(), track.GetEnd())
    ]
    if any(track.IsLocked() for track in attached) or via.IsLocked():
        return False
    deficit = VIA_MASK_CLEARANCE - gap + ROUTE_MARGIN
    for step in range(1, 9):
        distance = mm(deficit + 0.02 * (step - 1))
        for degrees in VIA_NUDGE_ANGLES:
            angle = math.radians(degrees)
            dx = push_x * math.cos(angle) - push_y * math.sin(angle)
            dy = push_x * math.sin(angle) + push_y * math.cos(angle)
            centre = pcbnew.VECTOR2I(
                round(origin.x + dx * distance), round(origin.y + dy * distance),
            )
            if not via_position_clear(board, via, centre):
                continue
            moved = []
            for track in attached:
                other = track.GetEnd() if track.GetStart() == origin else track.GetStart()
                moved.append((track, other))
            if not all(
                segment_clear_of_other_nets(
                    board, centre, other, track.GetWidth(), via.GetNetCode(),
                    track.GetLayer(),
                    pcbnew.F_Mask if track.GetLayer() == pcbnew.F_Cu else pcbnew.B_Mask,
                    via, attached,
                )
                for track, other in moved
                if track.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu)
            ):
                continue
            via.SetPosition(centre)
            for track, _ in moved:
                if track.GetStart() == origin:
                    track.SetStart(centre)
                else:
                    track.SetEnd(centre)
            return True
    return False


def nudge_vias(board: pcbnew.BOARD) -> None:
    for _ in range(50):
        if not any(
            nudge_via(board, via, gap, push_x, push_y)
            for gap, _, via, push_x, push_y in via_mask_violations(board)
            if push_x or push_y
        ):
            break


def check_via_mask_clearance(board: pcbnew.BOARD) -> None:
    failures = [
        f"{via.GetNetname()} via at "
        f"({pcbnew.ToMM(via.GetPosition().x):.3f}, "
        f"{pcbnew.ToMM(via.GetPosition().y):.3f}) is {gap:.3f} mm from "
        f"{pad.GetParentFootprint().GetReference()}.{pad.GetNumber()} mask opening"
        for gap, pad, via, _, _ in via_mask_violations(board)
    ]
    if failures:
        raise SystemExit(
            "Vias are too close to solder mask openings:\n" + "\n".join(failures)
        )
    print(
        f"PASS: vias clear square-cornered solder mask openings by >= "
        f"{VIA_MASK_CLEARANCE:.2f} mm"
    )


def check_mask_track_clearance(board: pcbnew.BOARD) -> None:
    failures = []
    for copper_layer, mask_layer in MASK_LAYERS:
        for gap, pad, track in mask_track_violations(
            board, copper_layer, mask_layer,
        ):
            failures.append(
                f"{board.GetLayerName(copper_layer)} {track.GetNetname()} "
                f"track is {gap:.3f} mm from "
                f"{pad.GetParentFootprint().GetReference()}.{pad.GetNumber()} "
                "mask opening"
            )
    if failures:
        raise SystemExit(
            "Solder mask opening exposes traces:\n" + "\n".join(failures)
        )
    print(
        f"PASS: every track clears unconnected solder mask openings by >= "
        f"{MASK_TRACK_CLEARANCE:.3f} mm"
    )


# JLCDFM soldermask bridge: warning 0.10-0.15 mm, good >= 0.15 mm. The stock
# 0.5 mm-pitch USB-C contacts leave 0.20 mm copper gaps; trimming each by
# 0.05 mm gives 0.25 mm gaps and 0.17 mm bridges at USB_C_MASK_EXPANSION.
MASK_BRIDGE_MIN = 0.16
USB_C_PAD_TRIM = 0.05
# JLCDFM slot width: danger < 0.61 mm, warning 0.61-0.81 mm. The shell slots
# keep length >= 2 x width, and each pad grows outward only, so the
# shell-to-contact gap is unchanged and the stock slot stays inside the new one.
SLOT_MIN_WIDTH = 0.82
USB_C_SLOT_WIDTH = 0.85
USB_C_SLOT_RING = 0.20
USB_C_SLOT_LENGTHS = {1.7: 1.9, 1.2: 1.75}  # stock slot length -> new length
# JLCDFM "THT to SMD": danger < 2.03 mm, warning 2.03-3.05 mm, good > 3.05 mm,
# measured between a plated hole's pad and another part's SMD pad. The Pico's
# left castellations sit 3.12 mm from the J2/J3 header pads.
THT_SMD_CLEARANCE = 3.07


def adjust_usb_c_pads(footprint: pcbnew.FOOTPRINT) -> None:
    for pad in footprint.Pads():
        # GetSize() returns a live reference; copy it before resizing.
        size = pcbnew.VECTOR2I(pad.GetSize(pcbnew.F_Cu))
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
            pad.SetSize(pcbnew.F_Cu, pcbnew.VECTOR2I(
                size.x - mm(USB_C_PAD_TRIM), size.y))
        elif pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            length = USB_C_SLOT_LENGTHS[round(pcbnew.ToMM(pad.GetDrillSize().y), 2)]
            width = USB_C_SLOT_WIDTH + 2 * USB_C_SLOT_RING
            pad.SetDrillSize(vec(USB_C_SLOT_WIDTH, length))
            pad.SetSize(pcbnew.F_Cu, vec(width, length + 2 * USB_C_SLOT_RING))
            local = pad.GetFPRelativePosition()
            shift = mm((width - pcbnew.ToMM(size.x)) / 2)
            pad.SetFPRelativePosition(pcbnew.VECTOR2I(
                local.x + (shift if local.x > 0 else -shift), local.y))


def check_slots(board: pcbnew.BOARD) -> None:
    failures = []
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            drill = pad.GetDrillSize()
            if pad.GetDrillShape() != pcbnew.PAD_DRILL_SHAPE_OBLONG or \
                    drill.x == drill.y:
                continue
            width = pcbnew.ToMM(min(drill.x, drill.y))
            length = pcbnew.ToMM(max(drill.x, drill.y))
            if width < SLOT_MIN_WIDTH or length < 2 * width:
                failures.append(
                    f"{footprint.GetReference()}.{pad.GetNumber()} slot "
                    f"{width:.2f} x {length:.2f} mm"
                )
    if failures:
        raise SystemExit("Plated slots too narrow or short:\n" + "\n".join(failures))
    print(f"PASS: plated slots are >= {SLOT_MIN_WIDTH:.2f} mm wide and >= 2:1")


def check_mask_bridges(board: pcbnew.BOARD) -> None:
    failures = []
    for copper_layer, mask_layer in MASK_LAYERS:
        openings = [
            (pad, opening) for pad, opening, _ in
            mask_openings(board, copper_layer, mask_layer)
        ]
        for index, (pad, opening) in enumerate(openings):
            box = opening.BBox()
            box.Inflate(mm(MASK_BRIDGE_MIN))
            for other, other_opening in openings[index + 1:]:
                if not box.Intersects(other_opening.BBox()):
                    continue
                outline = other_opening.Outline(0)
                gap = min(
                    math.sqrt(opening.SquaredDistanceToSeg(outline.CSegment(i)))
                    for i in range(outline.SegmentCount())
                )
                if 0 < pcbnew.ToMM(gap) < MASK_BRIDGE_MIN:
                    failures.append(
                        f"{board.GetLayerName(mask_layer)} "
                        f"{pad.GetParentFootprint().GetReference()}."
                        f"{pad.GetNumber()} to "
                        f"{other.GetParentFootprint().GetReference()}."
                        f"{other.GetNumber()}: {pcbnew.ToMM(gap):.3f} mm"
                    )
    if failures:
        raise SystemExit("Solder mask bridges too narrow:\n" + "\n".join(failures))
    print(f"PASS: solder mask bridges are >= {MASK_BRIDGE_MIN:.2f} mm or merged")


def check_tht_smd_clearance(board: pcbnew.BOARD) -> None:
    tht, smd = [], []
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                tht.append(pad)
            elif pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and \
                    pad.IsOnCopperLayer():
                smd.append(pad)
    failures = []
    for hole in tht:
        reference = hole.GetParentFootprint().GetReference()
        box = hole.GetBoundingBox()
        box.Inflate(mm(THT_SMD_CLEARANCE))
        for pad in smd:
            if pad.GetParentFootprint().GetReference() == reference or \
                    not box.Intersects(pad.GetBoundingBox()):
                continue
            layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
            gap = pcbnew.ToMM(hole.GetEffectiveShape(layer).GetClearance(
                pad.GetEffectiveShape(layer)))
            if gap < THT_SMD_CLEARANCE:
                failures.append(
                    f"{reference}.{hole.GetNumber()} to "
                    f"{pad.GetParentFootprint().GetReference()}."
                    f"{pad.GetNumber()}: {gap:.3f} mm"
                )
    if failures:
        raise SystemExit(
            "SMD pads too close to through-hole pads:\n" + "\n".join(failures)
        )
    print(
        f"PASS: SMD pads are >= {THT_SMD_CLEARANCE:.2f} mm from other "
        "parts' through-hole pads"
    )


def pad_position(
    board: pcbnew.BOARD,
    reference: str,
    number: str,
) -> tuple[float, float]:
    pad = board.FindFootprintByReference(reference).FindPadByNumber(number)
    position = pad.GetPosition()
    return pcbnew.ToMM(position.x), pcbnew.ToMM(position.y)


def add_locked_track_path(
    board: pcbnew.BOARD,
    net_name: str,
    layer: int,
    points: list[tuple[float, float]],
    width: float = SIGNAL_TRACK_WIDTH,
) -> None:
    net = board.FindNet(net_name)
    for first, second in zip(points, points[1:]):
        track = pcbnew.PCB_TRACK(board)
        track.SetStart(vec(*first))
        track.SetEnd(vec(*second))
        track.SetLayer(layer)
        track.SetWidth(mm(width))
        track.SetNet(net)
        track.SetLocked(True)
        board.Add(track)


def add_locked_via(
    board: pcbnew.BOARD,
    net_name: str,
    position: tuple[float, float],
) -> None:
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(vec(*position))
    via.SetWidth(mm(SIGNAL_VIA_DIAMETER))
    via.SetDrill(mm(SIGNAL_VIA_DRILL))
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    via.SetNet(board.FindNet(net_name))
    via.SetLocked(True)
    board.Add(via)


def add_critical_preroutes(board: pcbnew.BOARD) -> None:
    # Both clocks stay on F.Cu over the In1 GND plane. PICO_CLK taps R37
    # and slips between the R37/R38 pulls into U4; Z80_CLK threads between
    # Z80 pins 17/18 and runs inside the DIP rows (Clock clearance 0.30 mm).
    add_locked_track_path(
        board, "PICO_CLK", pcbnew.F_Cu,
        [pad_position(board, "A1", "4"), pad_position(board, "R37", "1"),
         rel(32.10, 48.31)[:2], rel(34.20, 48.31)[:2],
         rel(34.20, 43.865)[:2], pad_position(board, "U4", "2")],
    )
    add_locked_track_path(
        board, "Z80_CLK", pcbnew.F_Cu,
        [pad_position(board, "U4", "18"), rel(47.00, 45.135)[:2],
         rel(47.225, 44.91)[:2], rel(54.30, 44.91)[:2],
         rel(54.30, 16.95)[:2], rel(53.05, 15.70)[:2],
         pad_position(board, "U1", "6")],
    )
    # C7 turns its 3V3 pad toward U10 pin 20; a fixed F.Cu run keeps the
    # LVC245 bypass loop short.
    vcc = pad_position(board, "U10", "20")
    cap = pad_position(board, "C7", "1")
    add_locked_track_path(
        board, "+3V3", pcbnew.F_Cu,
        [vcc, (cap[0] - (cap[1] - vcc[1]), vcc[1]), cap],
        LOGIC_POWER_TRACK_WIDTH,
    )
    # Freerouting necks tracks down at the 0.3 mm USB-C contacts, so the
    # CC pull-downs are fixed straight runs to R41/R42 instead.
    cc1 = pad_position(board, "R41", "1")
    add_locked_track_path(
        board, "USB_CC1", pcbnew.F_Cu,
        [pad_position(board, "J1", "A5"),
         (cc1[0] + 0.325, pad_position(board, "J1", "A5")[1]), cc1],
        USB_TRACK_WIDTH,
    )
    cc2 = pad_position(board, "R42", "1")
    add_locked_track_path(
        board, "USB_CC2", pcbnew.F_Cu,
        [pad_position(board, "J1", "B5"),
         (cc2[0], pad_position(board, "J1", "B5")[1]), cc2],
        USB_TRACK_WIDTH,
    )


PLANE_NETS = ("GND", "+5V")
FANOUT_TRACK_WIDTH = 0.40
FANOUT_PAD_GAP = 0.20
FANOUT_CLEARANCE = 0.25


def rect_distance(
    x: float, y: float, rect: tuple[float, float, float, float]
) -> float:
    dx = max(rect[0] - x, 0.0, x - rect[2])
    dy = max(rect[1] - y, 0.0, y - rect[3])
    return math.hypot(dx, dy)


def segment_distance(
    x: float, y: float,
    start: tuple[float, float], end: tuple[float, float],
) -> float:
    sx, sy = start
    ex, ey = end
    length = (ex - sx) ** 2 + (ey - sy) ** 2
    t = 0.0 if length == 0 else max(0.0, min(1.0, (
        (x - sx) * (ex - sx) + (y - sy) * (ey - sy)
    ) / length))
    return math.hypot(x - (sx + t * (ex - sx)), y - (sy + t * (ey - sy)))


def pad_rect(pad: pcbnew.PAD) -> tuple[float, float, float, float]:
    box = pad.GetBoundingBox()
    return (
        pcbnew.ToMM(box.GetLeft()), pcbnew.ToMM(box.GetTop()),
        pcbnew.ToMM(box.GetRight()), pcbnew.ToMM(box.GetBottom()),
    )


def add_plane_fanouts(board: pcbnew.BOARD) -> None:
    # Every SMD plane pad gets its own locked stub and via to In1/In2, so
    # the autorouter only handles signals and decoupling paths stay short.
    via_radius = SIGNAL_VIA_DIAMETER / 2
    pads = [
        (pad, str(pad.GetNetname()), pad_rect(pad))
        for footprint in board.GetFootprints()
        for pad in footprint.Pads()
        # Paste-only apertures (Pico castellations) carry no copper.
        if pad.IsOnCopperLayer()
    ]
    tracks = [
        (str(track.GetNetname()),
         (pcbnew.ToMM(track.GetStart().x), pcbnew.ToMM(track.GetStart().y)),
         (pcbnew.ToMM(track.GetEnd().x), pcbnew.ToMM(track.GetEnd().y)),
         pcbnew.ToMM(track.GetWidth()) / 2)
        for track in board.GetTracks()
    ]
    vias: list[tuple[float, float]] = []
    served: set[tuple[str, float, float]] = set()
    keepouts = [
        (ANTENNA_LEFT, BOARD_TOP, ANTENNA_RIGHT, ANTENNA_BOTTOM),
    ] + [
        (x - 1.35, y - 1.35, x + 1.35, y + 1.35)
        for x, y in mounting_hole_table().values()
    ]
    edge_margin = COPPER_EDGE_CLEARANCE + via_radius

    def fits(net_name: str, start: tuple[float, float],
             via: tuple[float, float]) -> bool:
        x, y = via
        if not (BOARD_LEFT + edge_margin <= x <= BOARD_RIGHT - edge_margin
                and BOARD_TOP + edge_margin <= y
                <= BOARD_BOTTOM - edge_margin):
            return False
        if any(rect_distance(x, y, rect) < edge_margin for rect in keepouts):
            return False
        if any(math.hypot(x - vx, y - vy)
               < SIGNAL_VIA_DIAMETER + FANOUT_CLEARANCE for vx, vy in vias):
            return False
        samples = [
            (start[0] + (x - start[0]) * i / 8,
             start[1] + (y - start[1]) * i / 8) for i in range(9)
        ]
        for _, other, rect in pads:
            if other == net_name:
                # JLCDFM pad spacing ignores nets: keep the via off
                # neighbouring same-net pads, not just the one it serves.
                if rect_distance(*start, rect) > 0 and rect_distance(
                    x, y, rect,
                ) < via_radius + FANOUT_PAD_GAP:
                    return False
                continue
            if rect_distance(x, y, rect) < via_radius + FANOUT_CLEARANCE:
                return False
            if any(rect_distance(px, py, rect)
                   < FANOUT_TRACK_WIDTH / 2 + FANOUT_CLEARANCE
                   for px, py in samples):
                return False
        for other, first, second, half in tracks:
            if other == net_name:
                continue
            clearance = half + (
                CLOCK_CLEARANCE if other in {"PICO_CLK", "Z80_CLK"}
                else SIGNAL_CLEARANCE + ROUTE_MARGIN
            )
            if segment_distance(x, y, first, second) < \
                    via_radius + clearance:
                return False
            if any(segment_distance(px, py, first, second)
                   < FANOUT_TRACK_WIDTH / 2 + clearance
                   for px, py in samples):
                return False
        return True

    for pad, net_name, rect in sorted(
        pads, key=lambda item: (
            str(item[0].GetParentFootprint().GetReference()),
            str(item[0].GetNumber()),
        ),
    ):
        if net_name not in PLANE_NETS or \
                pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        # A plated hole on the same net already reaches the plane.
        if any(
            other.GetAttribute() == pcbnew.PAD_ATTRIB_PTH
            and str(other.GetNetname()) == net_name
            for other in pad.GetParentFootprint().Pads()
        ):
            continue
        cx = (rect[0] + rect[2]) / 2
        cy = (rect[1] + rect[3]) / 2
        # Stacked USB-C contacts share one land and one fanout.
        if (net_name, round(cx, 3), round(cy, 3)) in served:
            continue
        served.add((net_name, round(cx, 3), round(cy, 3)))
        body = pad.GetParentFootprint().GetPosition()
        ox = cx - pcbnew.ToMM(body.x)
        oy = cy - pcbnew.ToMM(body.y)
        outward = (
            (1.0 if ox >= 0 else -1.0, 0.0) if abs(ox) >= abs(oy)
            else (0.0, 1.0 if oy >= 0 else -1.0)
        )
        directions = [
            outward,
            (outward[1], outward[0]), (-outward[1], -outward[0]),
            (-outward[0], -outward[1]),
        ]
        placed = False
        for extra in (0.0, 0.4, 0.8):
            for dx, dy in directions:
                reach = (
                    (rect[2] - rect[0]) / 2 if dx
                    else (rect[3] - rect[1]) / 2
                ) + FANOUT_PAD_GAP + via_radius + extra
                via = (round(cx + dx * reach, 3), round(cy + dy * reach, 3))
                if fits(net_name, (cx, cy), via):
                    add_locked_track_path(
                        board, net_name, pcbnew.F_Cu, [(cx, cy), via],
                        FANOUT_TRACK_WIDTH,
                    )
                    add_locked_via(board, net_name, via)
                    vias.append(via)
                    tracks.append((net_name, (cx, cy), via,
                                   FANOUT_TRACK_WIDTH / 2))
                    placed = True
                    break
            if placed:
                break
        if not placed:
            raise SystemExit(
                f"no plane fanout room for "
                f"{pad.GetParentFootprint().GetReference()}."
                f"{pad.GetNumber()} ({net_name})"
            )


def add_mounting_holes(board: pcbnew.BOARD, footprint_root: Path) -> None:
    for reference, position in mounting_hole_table().items():
        footprint = load_footprint(
            footprint_root, "MountingHole:MountingHole_2.7mm_M2.5"
        )
        footprint.SetReference(reference)
        footprint.SetValue("M2.5 mounting hole")
        footprint.SetPosition(vec(*position))
        footprint.SetBoardOnly(True)
        footprint.Reference().SetVisible(False)
        footprint.Value().SetVisible(False)
        board.Add(footprint)


PLANE_ZONES = (
    ("INNER_GROUND_PLANE", "GND", pcbnew.In1_Cu),
    ("INNER_5V_PLANE", "+5V", pcbnew.In2_Cu),
)


def plane_zone_outline() -> tuple[tuple[float, float], ...]:
    return (
        (BOARD_LEFT + 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_BOTTOM - 0.5),
        (BOARD_LEFT + 0.5, BOARD_BOTTOM - 0.5),
    )


ANTENNA_BRIDGE_KEEPOUT = "Antenna Bridge Keep Out"
ANTENNA_BRIDGE_LAYERS = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)


def antenna_bridge_outline() -> tuple[tuple[float, float], ...]:
    # Covers the bridge and overlaps the window so no plane copper fills the
    # strip beyond the antenna end.
    return (
        (ANTENNA_LEFT, BOARD_TOP),
        (ANTENNA_RIGHT, BOARD_TOP),
        (ANTENNA_RIGHT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
        (ANTENNA_LEFT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
    )


def add_antenna_bridge_keepout(board: pcbnew.BOARD) -> None:
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetZoneName(ANTENNA_BRIDGE_KEEPOUT)
    layers = pcbnew.LSET()
    for layer in ANTENNA_BRIDGE_LAYERS:
        layers.AddLayer(layer)
    zone.SetLayerSet(layers)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(True)
    zone.SetDoNotAllowZoneFills(True)
    zone.SetDoNotAllowFootprints(False)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in antenna_bridge_outline():
        outline.Append(mm(x), mm(y))
    board.Add(zone)


def add_plane_zones(board: pcbnew.BOARD) -> None:
    for name, net_name, layer in PLANE_ZONES:
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(board.FindNet(net_name))
        zone.SetZoneName(name)
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        zone.SetLocalClearance(mm(0.3))
        zone.SetMinThickness(mm(0.25))
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in plane_zone_outline():
            outline.Append(mm(x), mm(y))
        board.Add(zone)


def build_board(
    manifest: dict[str, object],
    footprint_root: Path,
) -> pcbnew.BOARD:
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    board.SetLayerName(pcbnew.In1_Cu, "GND Plane")
    board.SetLayerType(pcbnew.In1_Cu, pcbnew.LT_MIXED)
    board.SetLayerName(pcbnew.In2_Cu, "+5V Plane")
    board.SetLayerType(pcbnew.In2_Cu, pcbnew.LT_POWER)
    settings = board.GetDesignSettings()
    settings.SetBoardThickness(mm(1.6))
    for name, clearance, width, via, drill, nets in NETCLASSES:
        if name == "Default":
            netclass = board.GetAllNetClasses()["Default"]
        else:
            netclass = pcbnew.NETCLASS(name)
        netclass.SetClearance(mm(clearance))
        netclass.SetTrackWidth(mm(width))
        netclass.SetViaDiameter(mm(via))
        netclass.SetViaDrill(mm(drill))
        if name != "Default":
            settings.m_NetSettings.SetNetclass(name, netclass)
        for net_name in nets:
            settings.m_NetSettings.SetNetclassPatternAssignment(
                net_name, name
            )
    settings.m_TrackMinWidth = mm(MIN_TRACK_WIDTH)
    settings.m_ViasMinSize = mm(MIN_VIA_DIAMETER)
    settings.m_ViasMinAnnularWidth = mm(MIN_VIA_ANNULAR)
    settings.m_MicroViasMinSize = mm(MIN_MICROVIA_DIAMETER)
    settings.m_MicroViasMinDrill = mm(MIN_MICROVIA_DRILL)
    settings.m_CopperEdgeClearance = mm(COPPER_EDGE_CLEARANCE)
    settings.m_SolderMaskExpansion = mm(SOLDER_MASK_EXPANSION)

    nets: dict[str, pcbnew.NETINFO_ITEM] = {}
    manifest_nets = manifest["nets"]
    assert isinstance(manifest_nets, dict)
    for name in sorted(manifest_nets):
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
        nets[name] = net

    endpoint_nets: dict[str, str] = {}
    for name, endpoints in manifest_nets.items():
        for endpoint in endpoints:
            endpoint_nets[endpoint] = name

    placements = placement_table()
    details = manifest["component_details"]
    assert isinstance(details, dict)
    details = {
        reference: detail
        for reference, detail in details.items()
        if detail["footprint"]
    }
    missing = sorted(set(details) - set(placements))
    extra = sorted(set(placements) - set(details))
    if missing or extra:
        raise SystemExit(
            f"placement table mismatch: missing={missing}, extra={extra}"
        )

    for reference in sorted(details):
        detail = details[reference]
        footprint_id = detail["footprint"]
        if not footprint_id:
            raise SystemExit(f"{reference} has no footprint")
        footprint = load_footprint(footprint_root, footprint_id)
        if reference == "A1":
            prepare_pico_footprint(footprint, footprint_root)
        merge_stacked_pad_openings(footprint)
        assign_3d_model(footprint)
        footprint.SetReference(reference)
        footprint.SetValue(detail["value"])
        x, y, rotation = placements[reference]
        footprint.SetPosition(vec(x, y))
        footprint.SetOrientationDegrees(rotation)
        footprint.Value().SetVisible(False)
        footprint.Reference().SetTextSize(vec(0.8, 0.8))
        footprint.Reference().SetTextThickness(mm(SILK_MIN_STROKE))
        footprint.Reference().SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        if reference.startswith("U"):
            center = footprint.GetBoundingBox(False, False).GetCenter()
            footprint.Reference().SetPosition(center)
            footprint.Reference().SetLayer(pcbnew.F_Fab)
        if reference in REFERENCE_POSITIONS:
            footprint.Reference().SetPosition(
                vec(*REFERENCE_POSITIONS[reference])
            )
        if reference in {"A2", "J1"}:
            footprint.Reference().SetVisible(False)
        if reference == "J1":
            adjust_usb_c_pads(footprint)
            for pad in footprint.Pads():
                pad.SetLocalSolderMaskMargin(mm(USB_C_MASK_EXPANSION))
        if reference.startswith(("C", "R", "D")):
            footprint.Reference().SetLayer(pcbnew.F_Fab)
        if reference in CHIP_PASSIVES:
            # Machine-placed 0603 parts: outlines would crowd the pads.
            for item in footprint.GraphicalItems():
                if item.GetLayer() == pcbnew.F_SilkS:
                    item.SetLayer(pcbnew.F_Fab)
        if reference == "J1":
            # The receptacle mouth overhangs the edge; keep silk on-board.
            for item in footprint.GraphicalItems():
                if item.GetLayer() == pcbnew.F_SilkS and pcbnew.ToMM(
                    item.GetBoundingBox().GetRight()
                ) > BOARD_RIGHT - 0.3:
                    item.SetLayer(pcbnew.F_Fab)
        if reference.startswith("U") and reference != "U1":
            # Stock SOIC/SOP pin-1 triangles crowd pad 1 and vias; a dot
            # outside the pad row marks pin 1 instead.
            for item in footprint.GraphicalItems():
                if item.GetLayer() == pcbnew.F_SilkS and \
                        item.GetShape() == pcbnew.SHAPE_T_POLY:
                    item.SetLayer(pcbnew.F_Fab)
            pad_box = footprint.FindPadByNumber("1").GetBoundingBox()
            dot = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
            dot.SetLayer(pcbnew.F_SilkS)
            dot.SetWidth(mm(SILK_MIN_STROKE))
            dot.SetFilled(True)
            dot_dx, dot_dy = PIN1_DOT_OFFSETS.get(reference, (-0.65, 0.0))
            center = vec(
                pcbnew.ToMM(pad_box.GetLeft()) + dot_dx,
                pcbnew.ToMM(pad_box.GetCenter().y) + dot_dy,
            )
            dot.SetCenter(center)
            dot.SetEnd(vec(pcbnew.ToMM(center.x) + 0.2, pcbnew.ToMM(center.y)))
            board.Add(dot)
        if reference == "A1":
            for item in footprint.GraphicalItems():
                if item.GetLayer() == pcbnew.F_SilkS:
                    item.SetLayer(pcbnew.F_Fab)
            footprint.Reference().SetVisible(False)
        board.Add(footprint)
        for pad in footprint.Pads():
            endpoint = f"{reference}.{pad.GetNumber()}"
            net_name = endpoint_nets.get(endpoint)
            if net_name is not None:
                pad.SetNet(nets[net_name])
            if net_name in {"GND", "+5V"}:
                pad.SetLocalZoneConnection(
                    pcbnew.ZONE_CONNECTION_FULL
                )

    add_critical_preroutes(board)
    add_plane_fanouts(board)
    add_mounting_holes(board, footprint_root)
    add_outline(board)
    add_plane_zones(board)
    add_antenna_bridge_keepout(board)
    add_text(
        board, "Z80-Nexus | Z80 | 20 MHz | 2 x 64K Banks",
        *TITLE_BACK_POSITION, 1.0, 90, layer=pcbnew.B_SilkS,
    )
    for text, x, y, size in assembly_labels():
        add_text(board, text, x, y, size)
    for text, x, y in HEADER_PIN_LABELS:
        add_text(board, text, x, y, 0.8, 90)
    add_text(
        board, "REV SMT-A", TITLE_BACK_POSITION[0] - 2.5,
        TITLE_BACK_POSITION[1], 0.8, 90, layer=pcbnew.B_SilkS,
    )
    # Front name stays visible with the Pico and OLED modules fitted.
    add_text(board, "Z80-Nexus SMT", *TITLE_FRONT_POSITION, 1.3)
    for reference in IC_LABELS:
        footprint = board.FindFootprintByReference(reference)
        center = footprint.GetBoundingBox(False, False).GetCenter()
        # Vertical DIP bodies read along their length.
        angle = 90 if placements[reference][2] == 0 else 0
        add_text(
            board,
            ic_label_text(reference),
            pcbnew.ToMM(center.x) + IC_LABEL_OFFSETS.get(reference, (0, 0))[0],
            pcbnew.ToMM(center.y) + IC_LABEL_OFFSETS.get(reference, (0, 0))[1],
            0.8,
            angle,
            # SMT packages cover their body labels; only the socketless DIP
            # Z80 keeps a printed label beside its pins.
            pcbnew.F_SilkS if reference == "U1" else pcbnew.F_Fab,
        )
    for item in silkscreen_items(board):
        if item.Type() in (pcbnew.PCB_TEXT_T, pcbnew.PCB_FIELD_T):
            item.SetTextThickness(max(
                item.GetTextThickness(), mm(SILK_MIN_STROKE)
            ))
        elif item.Type() == pcbnew.PCB_SHAPE_T:
            item.SetWidth(max(item.GetWidth(), mm(SILK_MIN_STROKE)))
    board.BuildConnectivity()
    return board


# JLCPCB JLC04161H-7628 style 1.6 mm stack: F.Cu and B.Cu each sit 0.21 mm
# above their reference plane (In1 GND, In2 +5V).
STACKUP = (
    ("F.SilkS", "Top Silk Screen", None, None),
    ("F.Paste", "Top Solder Paste", None, None),
    ("F.Mask", "Top Solder Mask", 0.01, None),
    ("F.Cu", "copper", 0.035, None),
    ("dielectric 1", "prepreg", 0.2104, "7628"),
    ("In1.Cu", "copper", 0.0152, None),
    ("dielectric 2", "core", 1.065, "FR4"),
    ("In2.Cu", "copper", 0.0152, None),
    ("dielectric 3", "prepreg", 0.2104, "7628"),
    ("B.Cu", "copper", 0.035, None),
    ("B.Mask", "Bottom Solder Mask", 0.01, None),
    ("B.Paste", "Bottom Solder Paste", None, None),
    ("B.SilkS", "Bottom Silk Screen", None, None),
)


def stackup_sexpr() -> str:
    lines = ["\t\t(stackup"]
    for name, kind, thickness, material in STACKUP:
        entry = f'\t\t\t(layer "{name}" (type "{kind}")'
        if thickness is not None:
            entry += f" (thickness {thickness})"
        if material is not None:
            entry += (
                f' (material "{material}") (epsilon_r 4.4)'
                " (loss_tangent 0.02)"
            )
        lines.append(entry + ")")
    lines += [
        '\t\t\t(copper_finish "HAL lead-free")',
        "\t\t\t(dielectric_constraints no)",
        "\t\t)",
    ]
    return "\n".join(lines) + "\n"


def write_stackup(board_path: Path) -> None:
    text = board_path.read_text(encoding="utf-8")
    if "(stackup" in text:
        raise SystemExit(f"{board_path} already contains a stackup")
    anchor = "\t(setup\n"
    if anchor not in text:
        raise SystemExit(f"{board_path} has no setup section")
    board_path.write_text(
        text.replace(anchor, anchor + stackup_sexpr(), 1), encoding="utf-8"
    )


def check_stackup(board_path: Path) -> None:
    if stackup_sexpr() not in board_path.read_text(encoding="utf-8"):
        raise SystemExit("PCB fabrication stackup is missing or stale")
    thickness = sum(entry[2] or 0 for entry in STACKUP)
    if abs(thickness - 1.6) > 0.02:
        raise SystemExit(f"PCB stackup totals {thickness:.3f} mm, not 1.6 mm")


def close_mm(actual: int, expected: float) -> bool:
    return abs(pcbnew.ToMM(actual) - expected) <= 0.001


def track_signatures(board: pcbnew.BOARD) -> set[tuple[object, ...]]:
    signatures: set[tuple[object, ...]] = set()
    for item in board.GetTracks():
        net_name = str(item.GetNetname())
        if item.Type() == pcbnew.PCB_VIA_T:
            via = pcbnew.Cast_to_PCB_VIA(item)
            position = item.GetPosition()
            layer_widths = tuple(
                (
                    layer,
                    round(pcbnew.ToMM(via.GetWidth(layer)), 4),
                )
                for layer in board.GetEnabledLayers().CuStack()
                if via.GetLayerSet().Contains(layer)
            )
            signatures.add((
                "via", net_name,
                round(pcbnew.ToMM(position.x), 4),
                round(pcbnew.ToMM(position.y), 4),
                int(via.GetViaType()),
                int(via.TopLayer()),
                int(via.BottomLayer()),
                layer_widths,
                round(pcbnew.ToMM(via.GetDrillValue()), 4),
            ))
            continue
        start = item.GetStart()
        end = item.GetEnd()
        endpoints = tuple(sorted((
            (round(pcbnew.ToMM(start.x), 4),
             round(pcbnew.ToMM(start.y), 4)),
            (round(pcbnew.ToMM(end.x), 4),
             round(pcbnew.ToMM(end.y), 4)),
        )))
        signatures.add((
            "track", net_name, item.GetLayer(),
            endpoints,
            round(pcbnew.ToMM(item.GetWidth()), 4),
        ))
    return signatures


def check_board(
    manifest: dict[str, object],
    board_path: Path,
    footprint_root: Path,
) -> None:
    check_stackup(board_path)
    board = pcbnew.LoadBoard(str(board_path))
    check_3d_models(board)
    if not close_mm(board.GetDesignSettings().GetBoardThickness(), 1.6):
        raise SystemExit("PCB board thickness must be 1.6 mm")
    manifest_nets = manifest["nets"]
    details = manifest["component_details"]
    assert isinstance(details, dict)
    details = {
        reference: detail
        for reference, detail in details.items()
        if detail["footprint"]
    }
    assert isinstance(manifest_nets, dict)
    assert isinstance(details, dict)

    expected: dict[str, str] = {}
    for net_name, endpoints in manifest_nets.items():
        for endpoint in endpoints:
            if endpoint.split(".", 1)[0] in details:
                expected[endpoint] = net_name

    footprints = {
        footprint.GetReference(): footprint
        for footprint in board.GetFootprints()
        if not footprint.GetReference().startswith("H")
    }
    missing = sorted(set(details) - set(footprints))
    extra = sorted(set(footprints) - set(details))
    if missing or extra:
        raise SystemExit(
            f"PCB footprint mismatch: missing={missing}, extra={extra}"
        )
    mounting_holes = {
        footprint.GetReference(): footprint
        for footprint in board.GetFootprints()
        if footprint.GetReference().startswith("H")
    }
    if set(mounting_holes) != set(mounting_hole_table()):
        raise SystemExit("PCB mounting-hole references are stale")
    for reference, (expected_x, expected_y) in mounting_hole_table().items():
        hole = mounting_holes[reference]
        position = hole.GetPosition()
        if (
            abs(pcbnew.ToMM(position.x) - expected_x) > 0.01
            or abs(pcbnew.ToMM(position.y) - expected_y) > 0.01
        ):
            raise SystemExit(f"{reference} mounting-hole placement is stale")
        pads = list(hole.Pads())
        if (
            str(hole.GetFPID().GetLibItemName()) != "MountingHole_2.7mm_M2.5"
            or len(pads) != 1
            or not close_mm(pads[0].GetDrillSize().x, 2.7)
            or not close_mm(pads[0].GetDrillSize().y, 2.7)
            or pads[0].GetAttribute() != pcbnew.PAD_ATTRIB_NPTH
        ):
            raise SystemExit(f"{reference} must be a 2.7 mm NPTH M2.5 hole")

    actual: dict[str, str] = {}
    placements = placement_table()
    for reference, footprint in footprints.items():
        expected_value = details[reference]["value"]
        if str(footprint.GetValue()) != expected_value:
            raise SystemExit(
                f"{reference} value mismatch: "
                f"{footprint.GetValue()} != {expected_value}"
            )
        expected_footprint = details[reference]["footprint"].split(":", 1)[-1]
        actual_footprint = str(footprint.GetFPID().GetLibItemName())
        if actual_footprint != expected_footprint:
            raise SystemExit(
                f"{reference} footprint mismatch: "
                f"{actual_footprint} != "
                f"{expected_footprint}"
            )
        expected_x, expected_y, expected_rotation = placements[reference]
        position = footprint.GetPosition()
        actual_x = pcbnew.ToMM(position.x)
        actual_y = pcbnew.ToMM(position.y)
        actual_rotation = footprint.GetOrientationDegrees() % 360
        if (
            abs(actual_x - expected_x) > 0.01
            or abs(actual_y - expected_y) > 0.01
            or abs(actual_rotation - expected_rotation) > 0.01
        ):
            raise SystemExit(
                f"{reference} placement mismatch: "
                f"({actual_x:.2f}, {actual_y:.2f}, {actual_rotation:.2f}) != "
                f"({expected_x:.2f}, {expected_y:.2f}, "
                f"{expected_rotation:.2f})"
            )
        for pad in footprint.Pads():
            endpoint = f"{reference}.{pad.GetNumber()}"
            if endpoint in expected:
                actual[endpoint] = str(pad.GetNetname())

    oled = footprints["A2"]
    oled_x, oled_y, oled_rotation = placements["A2"]
    for number, offset in (("GND", 0), ("VCC", 2.54), ("SCK", 5.08), ("SDA", 7.62)):
        pad = oled.FindPadByNumber(number)
        if (
            oled_rotation != 90
            or pad is None
            or not close_mm(pad.GetPosition().x, oled_x - offset)
            or not close_mm(pad.GetPosition().y, oled_y)
            or not close_mm(pad.GetDrillSize().x, 1.0)
        ):
            raise SystemExit(f"OLED {number} socket geometry is stale")
    # The module body rises from the socket toward the top edge; only
    # low-profile SMT parts may sit beneath it.
    body_left, body_right = oled_x - 9.81, oled_x + 2.19
    body_top, body_bottom = oled_y - 36.5, oled_y - 1.5
    for reference, footprint in footprints.items():
        if reference == "A2":
            continue
        box = footprint.GetCourtyard(pcbnew.F_CrtYd).BBox()
        if (
            pcbnew.ToMM(box.GetRight()) <= body_left
            or pcbnew.ToMM(box.GetLeft()) >= body_right
            or pcbnew.ToMM(box.GetBottom()) <= body_top
            or pcbnew.ToMM(box.GetTop()) >= body_bottom
        ):
            continue
        if not footprint.GetAttributes() & pcbnew.FP_SMD:
            raise SystemExit(f"{reference} is under the OLED but is not SMT")

    usb = footprints["J1"]
    usb_x, _, usb_rotation = placements["J1"]
    if (
        str(usb.GetFPID().GetLibItemName()) !=
            "USB_C_Receptacle_HRO_TYPE-C-31-M-12"
        or usb_rotation != 90
        or not close_mm(mm(usb_x), BOARD_RIGHT - USB_C_MOUTH_OFFSET)
    ):
        raise SystemExit("J1 USB-C mouth must sit flush with the right edge")
    usb_nets = {
        str(pad.GetNumber()): str(pad.GetNetname()) for pad in usb.Pads()
    }
    for number, net_name in (
        ("A4", "+5V"), ("A9", "+5V"), ("B4", "+5V"), ("B9", "+5V"),
        ("A1", "GND"), ("A12", "GND"), ("B1", "GND"), ("B12", "GND"),
        ("SH", "GND"), ("A5", "USB_CC1"), ("B5", "USB_CC2"),
        ("A6", ""), ("A7", ""), ("B6", ""), ("B7", ""),
        ("A8", ""), ("B8", ""),
    ):
        if usb_nets.get(number) != net_name:
            raise SystemExit(f"J1 {number} must connect to {net_name or 'nothing'}")

    missing_endpoints = sorted(set(expected) - set(actual))
    mismatched = sorted(
        endpoint for endpoint in expected
        if endpoint in actual and expected[endpoint] != actual[endpoint]
    )
    if missing_endpoints or mismatched:
        raise SystemExit(
            "PCB net mismatch: "
            f"missing={missing_endpoints}, mismatched={mismatched}"
        )
    if board.GetCopperLayerCount() != 4:
        raise SystemExit(
            f"PCB must have exactly four copper layers, got "
            f"{board.GetCopperLayerCount()}"
        )
    if (
        board.GetLayerType(pcbnew.In1_Cu) != pcbnew.LT_MIXED
        or board.GetLayerType(pcbnew.In2_Cu) != pcbnew.LT_POWER
    ):
        raise SystemExit("PCB internal-layer types are stale")
    critical_layers: dict[str, set[int]] = {
        "PICO_CLK": set(), "Z80_CLK": set(),
    }
    critical_vias = {"PICO_CLK": 0, "Z80_CLK": 0}
    locked_z80_clock_segments = 0
    for item in board.GetTracks():
        net_name = str(item.GetNetname())
        if item.Type() != pcbnew.PCB_VIA_T and \
                item.GetLayer() in {pcbnew.In1_Cu, pcbnew.In2_Cu}:
            raise SystemExit("inner plane layers must not contain tracks")
        if net_name == "Z80_CLK":
            if item.Type() != pcbnew.PCB_VIA_T and \
                    item.IsLocked() and item.GetLayer() == pcbnew.F_Cu:
                locked_z80_clock_segments += 1
        if net_name not in critical_layers:
            continue
        if not item.IsLocked():
            raise SystemExit(f"{net_name} contains unlocked copper")
        if item.Type() == pcbnew.PCB_VIA_T:
            critical_vias[net_name] += 1
        else:
            critical_layers[net_name].add(item.GetLayer())
    if critical_layers["PICO_CLK"] != {pcbnew.F_Cu} or \
            critical_vias["PICO_CLK"] != 0:
        raise SystemExit("PICO_CLK must remain a via-free F.Cu route")
    if critical_layers["Z80_CLK"] != {pcbnew.F_Cu} or \
            critical_vias["Z80_CLK"] != 0:
        raise SystemExit("Z80_CLK must remain a via-free F.Cu route")
    if locked_z80_clock_segments < 1:
        raise SystemExit(
            "Z80_CLK must retain its locked CPU path"
        )
    classes = {
        str(name): netclass
        for name, netclass in board.GetAllNetClasses().items()
    }
    for name, clearance, width, via, drill, nets in NETCLASSES:
        if name not in classes:
            raise SystemExit(f"PCB is missing {name} netclass")
        netclass = classes[name]
        if (
            not close_mm(netclass.GetClearance(), clearance)
            or not close_mm(netclass.GetTrackWidth(), width)
            or not close_mm(netclass.GetViaDiameter(), via)
            or not close_mm(netclass.GetViaDrill(), drill)
        ):
            raise SystemExit(f"PCB {name} netclass rules are stale")
        for net_name in nets:
            if str(board.FindNet(net_name).GetNetClassName()) != name:
                raise SystemExit(
                    f"{net_name} is not assigned to {name} netclass"
                )
    design = board.GetDesignSettings()
    for actual_rule, expected_rule, label in (
        (design.m_TrackMinWidth, MIN_TRACK_WIDTH, "minimum track width"),
        (design.m_ViasMinSize, MIN_VIA_DIAMETER, "minimum via diameter"),
        (design.m_ViasMinAnnularWidth, MIN_VIA_ANNULAR,
         "minimum via annular width"),
        (design.m_MicroViasMinSize, MIN_MICROVIA_DIAMETER,
         "minimum microvia diameter"),
        (design.m_MicroViasMinDrill, MIN_MICROVIA_DRILL,
         "minimum microvia drill"),
        (design.m_CopperEdgeClearance, COPPER_EDGE_CLEARANCE,
         "copper edge clearance"),
    ):
        if not close_mm(actual_rule, expected_rule):
            raise SystemExit(f"PCB {label} rule is stale")
    expected_outline = {
        tuple(sorted((
            (round(start[0], 2), round(start[1], 2)),
            (round(end[0], 2), round(end[1], 2)),
        )))
        for path in outline_paths()
        for start, end in zip(path, path[1:])
    }
    actual_outline = set()
    for drawing in board.GetDrawings():
        if drawing.GetLayer() != pcbnew.Edge_Cuts:
            continue
        start = drawing.GetStart()
        end = drawing.GetEnd()
        actual_outline.add(tuple(sorted((
            (round(pcbnew.ToMM(start.x), 2),
             round(pcbnew.ToMM(start.y), 2)),
            (round(pcbnew.ToMM(end.x), 2),
             round(pcbnew.ToMM(end.y), 2)),
        ))))
    if actual_outline != expected_outline:
        raise SystemExit("PCB outline does not match the generator")
    expected_zone_outline = plane_zone_outline()
    for zone_name, net_name, layer in PLANE_ZONES:
        matching_zones = [
            zone for zone in board.Zones()
            if str(zone.GetZoneName()) == zone_name
        ]
        if (
            len(matching_zones) != 1
            or str(matching_zones[0].GetNetname()) != net_name
            or matching_zones[0].GetLayer() != layer
            or matching_zones[0].GetPadConnection() !=
               pcbnew.ZONE_CONNECTION_THERMAL
            or not close_mm(matching_zones[0].GetLocalClearance(), 0.30)
            or not close_mm(matching_zones[0].GetMinThickness(), 0.25)
            or not matching_zones[0].IsFilled()
            or not matching_zones[0].HasFilledPolysForLayer(layer)
        ):
            raise SystemExit(f"PCB must contain one filled {zone_name}")
        actual_zone_outline = tuple(
            (round(pcbnew.ToMM(point.x), 2),
             round(pcbnew.ToMM(point.y), 2))
            for point in matching_zones[0].Outline().COutline(0).CPoints()
        )
        if actual_zone_outline != expected_zone_outline:
            raise SystemExit(f"{zone_name} outline is stale")
    pico = footprints["A1"]
    antenna_zones = [
        zone for zone in pico.Zones()
        if str(zone.GetZoneName()) == "Antenna Copper Keep Out"
    ]
    if (
        len(antenna_zones) != 1
        or not antenna_zones[0].GetIsRuleArea()
        or not antenna_zones[0].GetDoNotAllowTracks()
        or not antenna_zones[0].GetDoNotAllowVias()
        or not antenna_zones[0].GetDoNotAllowPads()
        or not antenna_zones[0].GetDoNotAllowZoneFills()
    ):
        raise SystemExit("Pico footprint is missing its antenna keepout")
    usb_zones = [
        zone for zone in pico.Zones()
        if str(zone.GetZoneName()) == "USB Cable"
    ]
    if len(usb_zones) != 1:
        raise SystemExit("Pico footprint is missing its USB cable envelope")
    usb_box = usb_zones[0].GetBoundingBox()
    if (
        usb_box.GetBottom() <= usb_box.GetTop()
        or abs(pcbnew.ToMM(usb_box.GetTop()) - BOARD_BOTTOM) > 0.50
        or pcbnew.ToMM(usb_box.GetBottom()) <= BOARD_BOTTOM
    ):
        raise SystemExit("Pico USB connector is not flush with the bottom edge")
    antenna_box = antenna_zones[0].GetBoundingBox()
    if (
        pcbnew.ToMM(antenna_box.GetLeft()) < ANTENNA_LEFT + ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetRight()) > ANTENNA_RIGHT - ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetTop()) < ANTENNA_TOP + ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetBottom()) > ANTENNA_BOTTOM - ANTENNA_KEEPOUT_MARGIN
    ):
        raise SystemExit("Pico antenna keepout is not contained by cutout")
    bridge_zones = [
        zone for zone in board.Zones()
        if str(zone.GetZoneName()) == ANTENNA_BRIDGE_KEEPOUT
    ]
    if (
        len(bridge_zones) != 1
        or not bridge_zones[0].GetIsRuleArea()
        or not bridge_zones[0].GetDoNotAllowTracks()
        or not bridge_zones[0].GetDoNotAllowVias()
        or not bridge_zones[0].GetDoNotAllowPads()
        or not bridge_zones[0].GetDoNotAllowZoneFills()
        or any(not bridge_zones[0].IsOnLayer(layer)
               for layer in ANTENNA_BRIDGE_LAYERS)
        or tuple(
            (round(pcbnew.ToMM(point.x), 2), round(pcbnew.ToMM(point.y), 2))
            for point in bridge_zones[0].Outline().COutline(0).CPoints()
        ) != tuple((round(x, 2), round(y, 2))
                   for x, y in antenna_bridge_outline())
    ):
        raise SystemExit("Antenna bridge copper keepout is missing or stale")
    for pad in pico.Pads():
        if not pad.IsOnCopperLayer():
            continue
        pad_box = pad.GetBoundingBox()
        left, right = pcbnew.ToMM(pad_box.GetLeft()), pcbnew.ToMM(pad_box.GetRight())
        top, bottom = pcbnew.ToMM(pad_box.GetTop()), pcbnew.ToMM(pad_box.GetBottom())
        gap_x = max(ANTENNA_LEFT - right, left - ANTENNA_RIGHT, 0.0)
        gap_y = max(ANTENNA_TOP - bottom, top - ANTENNA_BOTTOM, 0.0)
        if (gap_x * gap_x + gap_y * gap_y) ** 0.5 < ANTENNA_PAD_EDGE_CLEARANCE:
            raise SystemExit(
                f"A1.{pad.GetNumber()} copper is too close to the antenna cutout"
            )
    expected_labels = {
        (text, round(x, 2), round(y, 2))
        for text, x, y, _ in assembly_labels()
    }
    expected_labels |= {
        (text, round(x, 2), round(y, 2)) for text, x, y in HEADER_PIN_LABELS
    }
    actual_labels = set()
    for drawing in board.GetDrawings():
        if drawing.Type() != pcbnew.PCB_TEXT_T or \
           drawing.GetLayer() != pcbnew.F_SilkS:
            continue
        text = str(pcbnew.Cast_to_PCB_TEXT(drawing).GetText())
        if text not in {entry[0] for entry in expected_labels}:
            continue
        position = drawing.GetPosition()
        actual_labels.add((
            text,
            round(pcbnew.ToMM(position.x), 2),
            round(pcbnew.ToMM(position.y), 2),
        ))
    if actual_labels != expected_labels:
        raise SystemExit("PCB assembly silkscreen labels are stale")
    label_layers = {
        str(pcbnew.Cast_to_PCB_TEXT(drawing).GetText()): drawing.GetLayer()
        for drawing in board.GetDrawings()
        if drawing.Type() == pcbnew.PCB_TEXT_T
    }
    for reference in IC_LABELS:
        expected_layer = (
            pcbnew.F_SilkS if reference == "U1" else pcbnew.F_Fab
        )
        if label_layers.get(ic_label_text(reference)) != expected_layer:
            raise SystemExit(f"{reference} part-name label is missing")
    pin1_dots = sum(
        1 for drawing in board.GetDrawings()
        if drawing.Type() == pcbnew.PCB_SHAPE_T
        and drawing.GetLayer() == pcbnew.F_SilkS
        and drawing.GetShape() == pcbnew.SHAPE_T_CIRCLE
    )
    if pin1_dots != len(IC_LABELS) - 1:
        raise SystemExit("SMT IC pin-1 silkscreen dots are stale")
        if footprints[reference].Reference().GetLayer() != pcbnew.F_Fab:
            raise SystemExit(f"{reference} reference must stay on F.Fab")
    for reference in sorted(CHIP_PASSIVES) + ["A1"]:
        if any(
            item.GetLayer() == pcbnew.F_SilkS
            for item in footprints[reference].GraphicalItems()
        ):
            raise SystemExit(f"{reference} body graphics must remain on F.Fab")
    for reference in ("C8", "C9", "C10", "C11"):
        if not any(
            item.GetLayer() == pcbnew.F_SilkS
            for item in footprints[reference].GraphicalItems()
        ):
            raise SystemExit(f"{reference} must retain its polarity silkscreen")
    if not SESSION_PATH.is_file():
        raise SystemExit(f"missing routing session {SESSION_PATH}")
    session_board = build_board(manifest, footprint_root)
    if not pcbnew.ImportSpecctraSES(session_board, str(SESSION_PATH)):
        raise SystemExit(f"could not import routing session {SESSION_PATH}")
    straighten_pad_exits(session_board)
    nudge_vias(session_board)
    if track_signatures(session_board) != track_signatures(board):
        raise SystemExit("committed routing session does not reproduce PCB copper")
    check_silkscreen(board)
    check_mask_track_clearance(board)
    check_via_mask_clearance(board)
    check_mask_bridges(board)
    check_slots(board)
    check_tht_smd_clearance(board)
    print(
        f"PASS: PCB has {len(footprints)} schematic footprints, "
        f"{len(actual)} checked endpoints, and four copper layers"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--session", type=Path,
        help="Specctra .ses routing session to import",
    )
    parser.add_argument(
        "--output", type=Path, default=BOARD_PATH,
        help="output .kicad_pcb path",
    )
    parser.add_argument(
        "--dsn", type=Path, default=DSN_PATH,
        help="output Specctra .dsn path",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="validate the committed PCB against the current manifest",
    )
    parser.add_argument(
        "--sync-project-rules", action="store_true",
        help="write the generated PCB rules into the KiCad project",
    )
    args = parser.parse_args()

    if args.sync_project_rules:
        sync_project_rules()
        print(f"Wrote PCB design rules to {PROJECT_PATH}")
        return 0
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if args.check:
        check_project_rules()
        check_board(manifest, args.output, find_footprint_root())
        return 0
    board = build_board(manifest, find_footprint_root())
    args.dsn.parent.mkdir(parents=True, exist_ok=True)
    if not pcbnew.ExportSpecctraDSN(board, str(args.dsn)):
        raise SystemExit(f"could not write {args.dsn}")
    dsn_text = args.dsn.read_text(encoding="utf-8")
    _, separator, remainder = dsn_text.partition("\n")
    if not separator:
        raise SystemExit(f"invalid Specctra output in {args.dsn}")
    clearance_line = f"      (clearance {int(SIGNAL_CLEARANCE * 1000)})\n"
    if clearance_line not in remainder:
        raise SystemExit("Specctra output is missing the default clearance")
    remainder = remainder.replace(
        clearance_line,
        clearance_line +
        f"      (clearance {int(COPPER_EDGE_CLEARANCE * 1000)} "
        "(type wire_outline))\n",
        1,
    )
    remainder = remainder.replace(
        f"(clearance {int(SIGNAL_CLEARANCE * 1000)})",
        f"(clearance {int((SIGNAL_CLEARANCE + 0.01) * 1000)})",
    )
    args.dsn.write_text(
        f"(pcb {DSN_PATH.name}\n{remainder}",
        encoding="utf-8",
    )
    if args.session is not None:
        if not pcbnew.ImportSpecctraSES(board, str(args.session)):
            raise SystemExit(f"could not import routing session {args.session}")
        straighten_pad_exits(board)
        nudge_vias(board)
        board.BuildConnectivity()
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    gap_silkscreen_lines(board)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not pcbnew.SaveBoard(str(args.output), board):
        raise SystemExit(f"could not write {args.output}")
    write_stackup(args.output)
    if args.output.resolve() == BOARD_PATH.resolve():
        sync_project_rules()
        write_3d_models()
    print(f"Wrote {args.output}")
    print(f"Wrote {args.dsn}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
