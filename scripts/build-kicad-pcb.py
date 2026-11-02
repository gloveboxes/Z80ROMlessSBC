#!/usr/bin/env python3
"""Generate the placed four-layer PCB from the schematic net manifest.

Run this script with KiCad's bundled Python interpreter so the ``pcbnew``
module is available.  Pass ``--session`` to import a Specctra routing session
after generating the deterministic placement.
"""

from __future__ import annotations

import argparse
import json
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
KICAD_DIR = REPO_ROOT / "hardware" / "kicad"
MANIFEST_PATH = KICAD_DIR / "reports" / "net_manifest.json"
BOARD_PATH = KICAD_DIR / "z80_romless_sbc.kicad_pcb"
PROJECT_PATH = KICAD_DIR / "z80_romless_sbc.kicad_pro"
DSN_PATH = KICAD_DIR / "reports" / "z80_romless_sbc.dsn"
SESSION_PATH = KICAD_DIR / "reports" / "z80_romless_sbc.ses"

BOARD_LEFT = 5.0
BOARD_TOP = 5.0
BOARD_RIGHT = 90.0
BOARD_BOTTOM = 101.0
ANTENNA_LEFT = 47.60
ANTENNA_RIGHT = 59.45
ANTENNA_TOP = 16.40
ANTENNA_BOTTOM = 31.20
ANTENNA_CORNER_CHAMFER = 1.00
ANTENNA_KEEPOUT_MARGIN = 0.25
ANTENNA_PAD_EDGE_CLEARANCE = 0.60
SIGNAL_CLEARANCE = 0.20
SIGNAL_TRACK_WIDTH = 0.30
SIGNAL_VIA_DIAMETER = 0.80
SIGNAL_VIA_DRILL = 0.40
POWER_TRACK_WIDTH = 0.60
POWER_VIA_DIAMETER = 1.00
POWER_VIA_DRILL = 0.50
CLOCK_CLEARANCE = 0.30
MIN_TRACK_WIDTH = 0.20
MIN_VIA_DIAMETER = 0.60
MIN_VIA_ANNULAR = 0.15
MIN_MICROVIA_DIAMETER = 0.30
MIN_MICROVIA_DRILL = 0.10
COPPER_EDGE_CLEARANCE = 0.20


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


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
    })
    project["net_settings"]["classes"] = [
        project_netclass(
            "Default", SIGNAL_CLEARANCE, SIGNAL_TRACK_WIDTH,
            SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, 2147483647,
        ),
        project_netclass(
            "Power", SIGNAL_CLEARANCE, POWER_TRACK_WIDTH,
            POWER_VIA_DIAMETER, POWER_VIA_DRILL, -1,
        ),
        project_netclass(
            "Clock", CLOCK_CLEARANCE, SIGNAL_TRACK_WIDTH,
            SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL, -1,
        ),
    ]
    project["net_settings"]["netclass_patterns"] = [
        {"netclass": "Power", "pattern": net_name}
        for net_name in ("+5V", "+3V3", "VSYS")
    ] + [
        {"netclass": "Clock", "pattern": net_name}
        for net_name in ("PICO_CLK", "Z80_CLK")
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
    }
    for name, expected in expected_rules.items():
        if abs(float(rules.get(name, -1)) - expected) > 0.001:
            raise SystemExit(f"project rule {name} is stale")
    expected_classes = {
        "Default": (
            SIGNAL_CLEARANCE, SIGNAL_TRACK_WIDTH,
            SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL
        ),
        "Power": (
            SIGNAL_CLEARANCE, POWER_TRACK_WIDTH,
            POWER_VIA_DIAMETER, POWER_VIA_DRILL
        ),
        "Clock": (
            CLOCK_CLEARANCE, SIGNAL_TRACK_WIDTH,
            SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL
        ),
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
        (net_name, "Power") for net_name in ("+5V", "+3V3", "VSYS")
    } | {
        (net_name, "Clock") for net_name in ("PICO_CLK", "Z80_CLK")
    }
    if actual_patterns != expected_patterns:
        raise SystemExit("project Power netclass assignments are stale")


def load_footprint(root: Path, library_id: str) -> pcbnew.FOOTPRINT:
    library, name = library_id.split(":", 1)
    library_root = KICAD_DIR if library == "Z80SBC" else root
    footprint = pcbnew.FootprintLoad(str(library_root / f"{library}.pretty"), name)
    if footprint is None:
        raise SystemExit(f"cannot load footprint {library_id}")
    return footprint


def placement_table() -> dict[str, tuple[float, float, float]]:
    return {
        "A1": (7.8, 32.7, 90),
        "A2": (78.6, 20.7, 270),
        "U1": (74, 58, 270),
        "U2": (76.54, 83.4, 270),
        "U3": (16.51, 76.2, 0),
        "U4": (27.94, 76.2, 0),
        "U7": (15.24, 42.545, 0),
        "U9": (58.42, 43.18, 270),
        "U10": (61.595, 31.75, 0),
        "RN1": (47.625, 78.74, 0),
        "RN2": (77.47, 59.69, 270),
        "RN3": (38.1, 37.465, 0),
        "RN4": (12.065, 43.815, 270),
        "RN5": (29.845, 47.625, 90),
        "RN6": (26.035, 54.61, 90),
        "C1": (47.625, 55.245, 180),
        "C2": (80.3, 97.6, 90),
        "C3": (22.225, 72.39, 90),
        "C4": (41.275, 76.2, 180),
        "C5": (25.4, 37.465, 180),
        "C6": (53.975, 54.61, 180),
        "C7": (67.945, 27.305, 180),
        "C8": (8.6, 79.5, 0),
        "C9": (82.2, 62.6, 0),
        "C10": (82.2, 71.4, 0),
        "C11": (81.1, 87.7, 0),
        "D1": (14, 8.5, 0),
        "J1": (64.5, 16.65, 0),
        "J2": (40, 8.5, 270),
        "J3": (82, 78.2, 90),
    }


def mounting_hole_table() -> dict[str, tuple[float, float]]:
    return {
        "H1": (9, 9),
        "H2": (86, 9),
        "H3": (86, 97),
        "H4": (9, 97),
    }


# J2 OLED header pins, read vertically between the header and the Pico.
J2_PIN_LABELS = (
    ("SDA", 40.0, 11.9), ("SCL", 37.46, 11.9),
    ("3V3", 34.92, 11.9), ("GND", 32.38, 11.9),
)


def assembly_labels() -> list[tuple[str, float, float, float]]:
    return [
        ("C1", 46.4, 53.57, 0.8), ("C2", 82.2, 96.35, 0.8),
        ("C3", 20.4, 71.6, 0.8), ("C4", 40.0, 77.9, 0.8),
        ("C5", 24.15, 35.8, 0.8), ("C6", 52.7, 52.3, 0.8),
        ("C7", 66.7, 29.0, 0.8), ("C8\n22u", 9.6, 83.4, 0.8),
        ("C9\n22u", 87.6, 62.6, 0.8), ("C10\n22u", 87.6, 71.4, 0.8),
        ("C11\n100u", 87.6, 87.7, 0.8), ("D1", 19.1, 10.6, 0.8),
        ("A1 PICO 2 W", 30.0, 24.0, 1.0), ("USB", 7.8, 23.8, 0.8),
        ("+5V", 82.0, 76.2, 0.8), ("GND", 84.54, 76.2, 0.8),
        ("AUX 5V", 83.3, 80.3, 0.8), ("ONE SUPPLY", 69.0, 21.5, 0.8),
        ("J1 USB-C 5V", 69.0, 20.3, 0.8),
        ("OLED 38x12 3V3", 82.4, 22.6, 0.8),
    ]


IC_LABELS = {
    "U1": "Z80 CPU",
    "U2": "AS6C1008 SRAM",
    "U3": "74HCT32",
    "U4": "74AHCT244",
    "U7": "74LVC244",
    "U9": "74AHCT245",
    "U10": "74LVC245",
}


def ic_label_text(reference: str) -> str:
    return f"{IC_LABELS[reference]}\n{reference}"


REFERENCE_POSITIONS = {
    "RN2": (80.2, 81.9), "RN4": (8.6, 55.0), "RN6": (26.1, 42.5),
}


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


def add_outline(board: pcbnew.BOARD) -> None:
    outer = [
        (BOARD_LEFT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_TOP),
    ]
    cutout = [
        (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
        (ANTENNA_RIGHT - ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
        (ANTENNA_RIGHT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
        (ANTENNA_RIGHT, ANTENNA_BOTTOM - ANTENNA_CORNER_CHAMFER),
        (ANTENNA_RIGHT - ANTENNA_CORNER_CHAMFER, ANTENNA_BOTTOM),
        (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_BOTTOM),
        (ANTENNA_LEFT, ANTENNA_BOTTOM - ANTENNA_CORNER_CHAMFER),
        (ANTENNA_LEFT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
        (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
    ]
    for start, end in list(zip(outer, outer[1:])) + \
            list(zip(cutout, cutout[1:])):
        add_segment(board, pcbnew.Edge_Cuts, start, end, 0.25)


def add_text(
    board: pcbnew.BOARD,
    text: str,
    x: float,
    y: float,
    size: float,
    angle: float = 0,
) -> None:
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetLayer(pcbnew.F_SilkS)
    item.SetPosition(vec(x, y))
    item.SetTextSize(vec(size, size))
    item.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    item.SetTextThickness(mm(min(0.25, max(0.12, size * 0.12))))
    board.Add(item)


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
) -> None:
    net = board.FindNet(net_name)
    for first, second in zip(points, points[1:]):
        track = pcbnew.PCB_TRACK(board)
        track.SetStart(vec(*first))
        track.SetEnd(vec(*second))
        track.SetLayer(layer)
        track.SetWidth(mm(SIGNAL_TRACK_WIDTH))
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
    # Both clocks stay on F.Cu over the In1 GND plane. They thread between
    # DIP pins (Clock clearance 0.30 mm) instead of detouring around bodies.
    add_locked_track_path(
        board, "Z80_CLK", pcbnew.F_Cu,
        [pad_position(board, "U4", "18"), (38.10, 81.28),
         (44.79, 74.59), (44.79, 72.60),
         (58.09, 59.30), (60.00, 59.30), pad_position(board, "U1", "6")],
    )
    add_locked_track_path(
        board, "PICO_CLK", pcbnew.F_Cu,
        [pad_position(board, "A1", "4"), (15.70, 32.70), (24.70, 41.70),
         (24.70, 50.735), pad_position(board, "RN6", "2"), (27.30, 53.335),
         (27.30, 57.10), (27.01, 57.39), (27.01, 73.90), (27.91, 74.80),
         (28.80, 74.80), (29.20, 75.20), (29.20, 77.48),
         pad_position(board, "U4", "2")],
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
    default_class = board.GetAllNetClasses()["Default"]
    default_class.SetClearance(mm(SIGNAL_CLEARANCE))
    default_class.SetTrackWidth(mm(SIGNAL_TRACK_WIDTH))
    default_class.SetViaDiameter(mm(SIGNAL_VIA_DIAMETER))
    default_class.SetViaDrill(mm(SIGNAL_VIA_DRILL))
    power_class = pcbnew.NETCLASS("Power")
    power_class.SetClearance(mm(SIGNAL_CLEARANCE))
    power_class.SetTrackWidth(mm(POWER_TRACK_WIDTH))
    power_class.SetViaDiameter(mm(POWER_VIA_DIAMETER))
    power_class.SetViaDrill(mm(POWER_VIA_DRILL))
    settings.m_NetSettings.SetNetclass("Power", power_class)
    for power_net in ("+5V", "+3V3", "VSYS"):
        settings.m_NetSettings.SetNetclassPatternAssignment(
            power_net, "Power"
        )
    clock_class = pcbnew.NETCLASS("Clock")
    clock_class.SetClearance(mm(CLOCK_CLEARANCE))
    clock_class.SetTrackWidth(mm(SIGNAL_TRACK_WIDTH))
    clock_class.SetViaDiameter(mm(SIGNAL_VIA_DIAMETER))
    clock_class.SetViaDrill(mm(SIGNAL_VIA_DRILL))
    settings.m_NetSettings.SetNetclass("Clock", clock_class)
    for clock_net in ("PICO_CLK", "Z80_CLK"):
        settings.m_NetSettings.SetNetclassPatternAssignment(
            clock_net, "Clock"
        )
    settings.m_TrackMinWidth = mm(MIN_TRACK_WIDTH)
    settings.m_ViasMinSize = mm(MIN_VIA_DIAMETER)
    settings.m_ViasMinAnnularWidth = mm(MIN_VIA_ANNULAR)
    settings.m_MicroViasMinSize = mm(MIN_MICROVIA_DIAMETER)
    settings.m_MicroViasMinDrill = mm(MIN_MICROVIA_DRILL)
    settings.m_CopperEdgeClearance = mm(COPPER_EDGE_CLEARANCE)

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
        footprint.SetReference(reference)
        footprint.SetValue(detail["value"])
        x, y, rotation = placements[reference]
        footprint.SetPosition(vec(x, y))
        footprint.SetOrientationDegrees(rotation)
        footprint.Value().SetVisible(False)
        footprint.Reference().SetTextSize(vec(0.8, 0.8))
        footprint.Reference().SetTextThickness(mm(0.12))
        footprint.Reference().SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        if reference.startswith("U"):
            center = footprint.GetBoundingBox(False, False).GetCenter()
            footprint.Reference().SetPosition(center)
            footprint.Reference().SetLayer(pcbnew.F_Fab)
        if reference.startswith("RN"):
            footprint.Reference().SetPosition(vec(x + 4, y - 2.5))
        if reference in REFERENCE_POSITIONS:
            footprint.Reference().SetPosition(
                vec(*REFERENCE_POSITIONS[reference])
            )
        if reference in {"A2", "J1"}:
            footprint.Reference().SetVisible(False)
        if reference.startswith("C"):
            footprint.Reference().SetLayer(pcbnew.F_Fab)
            if reference in {f"C{index}" for index in range(1, 8)}:
                for item in footprint.GraphicalItems():
                    if item.GetLayer() == pcbnew.F_SilkS:
                        item.SetLayer(pcbnew.F_Fab)
        if reference == "D1":
            footprint.Reference().SetLayer(pcbnew.F_Fab)
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
            if endpoint in {"A1.3", "A1.23", "A1.28"}:
                pad.SetLocalClearance(mm(0.19))
            if net_name in {"GND", "+5V"}:
                pad.SetLocalZoneConnection(
                    pcbnew.ZONE_CONNECTION_FULL
                )

    add_critical_preroutes(board)
    add_mounting_holes(board, footprint_root)
    add_outline(board)
    add_plane_zones(board)
    add_text(board, "Z80 ROMless SBC", 50.5, 8.0, 1.0)
    for text, x, y, size in assembly_labels():
        add_text(board, text, x, y, size)
    for text, x, y in J2_PIN_LABELS:
        add_text(board, text, x, y, 0.8, 90)
    add_text(board, "20MHz target  REV B 4L", 50.5, 10.5, 0.8)
    for reference in IC_LABELS:
        footprint = board.FindFootprintByReference(reference)
        center = footprint.GetBoundingBox(False, False).GetCenter()
        # Vertical DIP bodies read along their length.
        angle = 90 if placements[reference][2] == 0 else 0
        add_text(
            board,
            ic_label_text(reference),
            pcbnew.ToMM(center.x),
            pcbnew.ToMM(center.y),
            0.8,
            angle,
        )
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
    oled_x, oled_y, _ = placements["A2"]
    for number, offset in (("GND", 0), ("VCC", 2.54), ("SCK", 5.08), ("SDA", 7.62)):
        pad = oled.FindPadByNumber(number)
        if (
            pad is None
            or not close_mm(pad.GetPosition().x, oled_x + offset)
            or not close_mm(pad.GetPosition().y, oled_y)
            or not close_mm(pad.GetDrillSize().x, 1.0)
        ):
            raise SystemExit(f"OLED {number} socket geometry is stale")

    usb_power = footprints["J1"]
    usb_x, usb_y, _ = placements["J1"]
    usb_pins = ("CC2", "D+", "D-", "CC1", "GND", "VBUS")
    if {str(pad.GetNumber()) for pad in usb_power.Pads()} != set(usb_pins):
        raise SystemExit("J1 must retain all six soldered module pins")
    for index, number in enumerate(usb_pins):
        pad = usb_power.FindPadByNumber(number)
        expected_net = {"GND": "GND", "VBUS": "+5V"}.get(number, "")
        if (
            not close_mm(pad.GetPosition().x, usb_x + index * 2.54)
            or not close_mm(pad.GetPosition().y, usb_y)
            or not close_mm(pad.GetDrillSize().x, 1.0)
            or not close_mm(pad.GetDrillSize().y, 1.0)
            or pad.GetAttribute() != pcbnew.PAD_ATTRIB_PTH
            or str(pad.GetNetname()) != expected_net
        ):
            raise SystemExit(f"J1 {number} solder geometry/net is stale")
    usb_courtyard = usb_power.GetCourtyard(pcbnew.F_CrtYd).BBox()
    if (
        abs(pcbnew.ToMM(usb_courtyard.GetTop()) - BOARD_TOP) > 0.05
        or pcbnew.ToMM(usb_courtyard.GetRight()) > BOARD_RIGHT
    ):
        raise SystemExit("J1 USB-C opening must sit flush with the top edge")

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
    for name, clearance, width, via, drill in (
        ("Default", SIGNAL_CLEARANCE, SIGNAL_TRACK_WIDTH,
         SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL),
        ("Power", SIGNAL_CLEARANCE, POWER_TRACK_WIDTH,
         POWER_VIA_DIAMETER, POWER_VIA_DRILL),
        ("Clock", CLOCK_CLEARANCE, SIGNAL_TRACK_WIDTH,
         SIGNAL_VIA_DIAMETER, SIGNAL_VIA_DRILL),
    ):
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
    for net_name in ("+5V", "+3V3", "VSYS"):
        if str(board.FindNet(net_name).GetNetClassName()) != "Power":
            raise SystemExit(f"{net_name} is not assigned to Power netclass")
    for net_name in ("PICO_CLK", "Z80_CLK"):
        if str(board.FindNet(net_name).GetNetClassName()) != "Clock":
            raise SystemExit(f"{net_name} is not assigned to Clock netclass")
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
    expected_paths = [
        [
            (BOARD_LEFT, BOARD_TOP),
            (BOARD_RIGHT, BOARD_TOP),
            (BOARD_RIGHT, BOARD_BOTTOM),
            (BOARD_LEFT, BOARD_BOTTOM),
            (BOARD_LEFT, BOARD_TOP),
        ],
        [
            (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
            (ANTENNA_RIGHT - ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
            (ANTENNA_RIGHT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
            (ANTENNA_RIGHT, ANTENNA_BOTTOM - ANTENNA_CORNER_CHAMFER),
            (ANTENNA_RIGHT - ANTENNA_CORNER_CHAMFER, ANTENNA_BOTTOM),
            (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_BOTTOM),
            (ANTENNA_LEFT, ANTENNA_BOTTOM - ANTENNA_CORNER_CHAMFER),
            (ANTENNA_LEFT, ANTENNA_TOP + ANTENNA_CORNER_CHAMFER),
            (ANTENNA_LEFT + ANTENNA_CORNER_CHAMFER, ANTENNA_TOP),
        ],
    ]
    expected_outline = {
        tuple(sorted((start, end)))
        for path in expected_paths
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
        usb_box.GetRight() <= usb_box.GetLeft()
        or abs(pcbnew.ToMM(usb_box.GetRight()) - BOARD_LEFT) > 0.50
        or pcbnew.ToMM(usb_box.GetLeft()) >= BOARD_LEFT
    ):
        raise SystemExit("Pico USB connector is not flush with the left edge")
    antenna_box = antenna_zones[0].GetBoundingBox()
    if (
        pcbnew.ToMM(antenna_box.GetLeft()) < ANTENNA_LEFT + ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetRight()) > ANTENNA_RIGHT - ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetTop()) < ANTENNA_TOP + ANTENNA_KEEPOUT_MARGIN
        or pcbnew.ToMM(antenna_box.GetBottom()) > ANTENNA_BOTTOM - ANTENNA_KEEPOUT_MARGIN
    ):
        raise SystemExit("Pico antenna keepout is not contained by cutout")
    for pad in pico.Pads():
        pad_box = pad.GetBoundingBox()
        left, right = pcbnew.ToMM(pad_box.GetLeft()), pcbnew.ToMM(pad_box.GetRight())
        top, bottom = pcbnew.ToMM(pad_box.GetTop()), pcbnew.ToMM(pad_box.GetBottom())
        if right <= ANTENNA_LEFT or left >= ANTENNA_RIGHT:
            continue
        if not (
            bottom <= ANTENNA_TOP - ANTENNA_PAD_EDGE_CLEARANCE
            or top >= ANTENNA_BOTTOM + ANTENNA_PAD_EDGE_CLEARANCE
        ):
            raise SystemExit(
                f"A1.{pad.GetNumber()} copper is too close to the antenna cutout"
            )
    expected_labels = {
        (text, round(x, 2), round(y, 2))
        for text, x, y, _ in assembly_labels()
    }
    expected_labels |= {
        (text, round(x, 2), round(y, 2)) for text, x, y in J2_PIN_LABELS
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
    silk_texts = {
        str(pcbnew.Cast_to_PCB_TEXT(drawing).GetText())
        for drawing in board.GetDrawings()
        if drawing.Type() == pcbnew.PCB_TEXT_T
        and drawing.GetLayer() == pcbnew.F_SilkS
    }
    for reference in IC_LABELS:
        if ic_label_text(reference) not in silk_texts:
            raise SystemExit(f"{reference} silkscreen part-name label is missing")
        if footprints[reference].Reference().GetLayer() != pcbnew.F_Fab:
            raise SystemExit(f"{reference} reference must stay on F.Fab")
    for reference in [f"C{index}" for index in range(1, 8)] + ["A1"]:
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
    if track_signatures(session_board) != track_signatures(board):
        raise SystemExit("committed routing session does not reproduce PCB copper")
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
        board.BuildConnectivity()
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not pcbnew.SaveBoard(str(args.output), board):
        raise SystemExit(f"could not write {args.output}")
    write_stackup(args.output)
    if args.output.resolve() == BOARD_PATH.resolve():
        sync_project_rules()
    print(f"Wrote {args.output}")
    print(f"Wrote {args.dsn}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
