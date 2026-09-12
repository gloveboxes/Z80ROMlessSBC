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
BOARD_RIGHT = 165.0
BOARD_BOTTOM = 140.0
ANTENNA_LEFT = 47.60
ANTENNA_RIGHT = 59.45
ANTENNA_TOP = 117.30
ANTENNA_BOTTOM = 132.90
ANTENNA_CORNER_CHAMFER = 1.00
SIGNAL_CLEARANCE = 0.20
SIGNAL_TRACK_WIDTH = 0.30
SIGNAL_VIA_DIAMETER = 0.80
SIGNAL_VIA_DRILL = 0.40
POWER_TRACK_WIDTH = 0.60
POWER_VIA_DIAMETER = 1.00
POWER_VIA_DRILL = 0.50
CLOCK_CLEARANCE = 0.40
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
    footprint = pcbnew.FootprintLoad(str(root / f"{library}.pretty"), name)
    if footprint is None:
        raise SystemExit(f"cannot load footprint {library_id}")
    return footprint


def placement_table() -> dict[str, tuple[float, float, float]]:
    placements: dict[str, tuple[float, float, float]] = {
        "J1": (16, 13, 0),
        "D1": (15, 105, 0),
        "U2": (10, 22, 0),
        "U1": (40, 17, 0),
        "U8": (78, 22, 0),
        "Q1": (86, 62, 0),
        "U3": (94, 22, 0),
        "U4": (58, 70, 90),
        "U9": (105, 55, 0),
        "U10": (124, 55, 0),
        "U7": (143, 55, 0),
        "A1": (7.8, 134, 90),
        "RN1": (8, 67, 0),
        "RN2": (34, 72, 0),
        "RN3": (82, 137, 0),
        "RN4": (30, 90, 0),
        "RN5": (72, 90, 0),
        "C1": (34, 42.5, 0),
        "C2": (28, 22, 0),
        "C3": (105, 30, 0),
        "C4": (58, 59, 0),
        "C5": (155, 55, 90),
        "C6": (71, 43, 0),
        "C7": (115.2, 55, 90),
        "C8": (134.2, 55, 90),
        "C9": (32, 61, 0),
        "C10": (96, 82, 0),
        "C11": (158, 82, 0),
        "C12": (27, 12, 0),
        "R29": (110, 135, 90),
        "R30": (118, 135, 90),
        "R31": (126, 135, 90),
        "TP1": (43, 10, 0),
        "TP2": (85, 68, 0),
        "TP3": (50, 10, 0),
        "TP4": (74, 10, 0),
        "TP5": (81, 10, 0),
        "TP6": (88, 10, 0),
        "TP7": (95, 10, 0),
    }
    for index in range(17, 22):
        placements[f"R{index}"] = (105 + (index - 17) * 9, 94, 90)
    placements["R22"] = (60, 105, 90)
    for index in range(23, 29):
        placements[f"R{index}"] = (110 + (index - 23) * 8, 115, 90)
    placements["R24"] = (68, 105, 90)
    placements["R25"] = (76, 105, 90)
    placements["R26"] = (84, 105, 90)
    return placements


def mounting_hole_table() -> dict[str, tuple[float, float]]:
    return {
        "H1": (9, 9),
        "H2": (160, 9),
        "H3": (160, 136),
        "H4": (135, 136),
    }


def assembly_labels() -> list[tuple[str, float, float, float]]:
    return [
        ("C1", 31.0, 42.5, 0.8),
        ("C2", 31.0, 26.0, 0.8),
        ("C3", 105.0, 35.0, 0.8),
        ("C4", 67.0, 59.0, 0.8),
        ("C5", 158.0, 55.0, 0.8),
        ("C6", 68.0, 47.0, 0.8),
        ("C7", 117.5, 60.0, 0.8),
        ("C8", 136.5, 60.0, 0.8),
        ("C9", 37.0, 61.0, 0.8),
        ("C10", 96.0, 87.0, 0.8),
        ("C11", 158.0, 76.0, 0.8),
        ("C12", 27.0, 19.0, 0.8),
        ("D1", 20.0, 99.0, 0.8),
        ("A1 PICO 2 W", 28.0, 113.0, 1.0),
        ("USB", 7.0, 108.0, 0.8),
    ]


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
) -> None:
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetLayer(pcbnew.F_SilkS)
    item.SetPosition(vec(x, y))
    item.SetTextSize(vec(size, size))
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
    u4_clock = pad_position(board, "U4", "18")
    z80_clock = pad_position(board, "U1", "6")
    z80_clock_junction = (u4_clock[0], 67.5)
    add_locked_track_path(
        board,
        "Z80_CLK",
        pcbnew.F_Cu,
        [
            u4_clock,
            z80_clock_junction,
            (38.0, 67.5),
            (38.0, z80_clock[1]),
            z80_clock,
        ],
    )
    pico_clock = pad_position(board, "A1", "4")
    clock_input = pad_position(board, "U4", "2")
    clock_bias = pad_position(board, "R22", "1")
    clock_junction = (45.0, 100.0)
    add_locked_track_path(
        board,
        "PICO_CLK",
        pcbnew.F_Cu,
        [
            pico_clock,
            (pico_clock[0], 137.0),
            (6.4, 137.0),
            (6.4, 100.0),
            clock_junction,
            (55.6, 93.0),
            (55.6, 76.0),
            (clock_input[0], 73.0),
            clock_input,
        ],
    )
    add_locked_track_path(
        board,
        "PICO_CLK",
        pcbnew.F_Cu,
        [clock_junction, clock_bias],
    )

    d4_z80 = pad_position(board, "U1", "7")
    d4_sram = pad_position(board, "U2", "18")
    d4_up = pad_position(board, "U9", "14")
    d4_down = pad_position(board, "U10", "14")
    d4_left_junction = (47.97, 76.0)
    d4_right_junction = (118.207, 69.0401)
    add_locked_track_path(
        board,
        "D4",
        pcbnew.In2_Cu,
        [
            d4_z80,
            (d4_left_junction[0], d4_z80[1]),
            d4_left_junction,
            (102.0, 76.0),
            (102.0, 88.92),
            (118.207, 88.92),
            d4_right_junction,
        ],
    )
    add_locked_track_path(
        board,
        "D4",
        pcbnew.In2_Cu,
        [d4_sram, (30.0, d4_sram[1]), (30.0, 76.0), d4_left_junction],
    )
    add_locked_track_path(
        board,
        "D4",
        pcbnew.In2_Cu,
        [d4_right_junction, (113.8199, 69.0401), d4_up],
    )
    add_locked_track_path(
        board,
        "D4",
        pcbnew.In2_Cu,
        [d4_right_junction, (130.4201, 69.0401), d4_down],
    )

    pico_d3 = pad_position(board, "A1", "17")
    pico_d3_pull = pad_position(board, "RN3", "5")
    pico_d3_via = (65.5096, 137.7818)
    add_locked_track_path(
        board,
        "PICO_D3",
        pcbnew.In2_Cu,
        [pico_d3, (52.2218, 137.7818), pico_d3_via],
    )
    add_locked_via(board, "PICO_D3", pico_d3_via)
    add_locked_track_path(
        board,
        "PICO_D3",
        pcbnew.F_Cu,
        [
            pico_d3_via,
            (67.4899, 135.8015),
            (90.9615, 135.8015),
            pico_d3_pull,
        ],
    )


def add_mounting_holes(board: pcbnew.BOARD, footprint_root: Path) -> None:
    for reference, position in mounting_hole_table().items():
        footprint = load_footprint(
            footprint_root, "MountingHole:MountingHole_3.2mm_M3"
        )
        footprint.SetReference(reference)
        footprint.SetValue("M3 mounting hole")
        footprint.SetPosition(vec(*position))
        footprint.SetBoardOnly(True)
        footprint.Reference().SetVisible(False)
        footprint.Value().SetVisible(False)
        board.Add(footprint)


def add_ground_zone(board: pcbnew.BOARD, ground: pcbnew.NETINFO_ITEM) -> None:
    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.In1_Cu)
    zone.SetNet(ground)
    zone.SetZoneName("INNER_GROUND_PLANE")
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    zone.SetLocalClearance(mm(0.3))
    zone.SetMinThickness(mm(0.25))
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in (
        (BOARD_LEFT + 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_BOTTOM - 0.5),
        (BOARD_LEFT + 0.5, BOARD_BOTTOM - 0.5),
    ):
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
    board.SetLayerName(pcbnew.In2_Cu, "Inner Signal")
    board.SetLayerType(pcbnew.In2_Cu, pcbnew.LT_SIGNAL)
    settings = board.GetDesignSettings()
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
        if reference.startswith("C"):
            footprint.Reference().SetLayer(pcbnew.F_Fab)
            for item in footprint.GraphicalItems():
                if item.GetLayer() == pcbnew.F_SilkS:
                    item.SetLayer(pcbnew.F_Fab)
        if reference == "D1":
            footprint.Reference().SetLayer(pcbnew.F_Fab)
        if reference == "U4":
            footprint.Reference().SetPosition(vec(70.0, 73.0))
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
            if endpoint in {"A1.23", "A1.28"}:
                pad.SetLocalClearance(mm(0.19))
            if endpoint in {
                "A1.3", "A1.8", "A1.13", "A1.18", "A1.23",
                "C4.2", "C6.2",
                "C8.2", "C10.2", "C11.2", "J1.2", "Q1.1", "R27.2",
                "R30.2", "RN3.1", "U1.29", "U3.12", "U8.10",
                "U8.15", "U9.10",
            }:
                pad.SetLocalZoneConnection(
                    pcbnew.ZONE_CONNECTION_FULL
                )

    add_critical_preroutes(board)
    add_mounting_holes(board, footprint_root)
    add_outline(board)
    add_text(board, "Z80 ROMless SBC", 130, 9, 2.0)
    for text, x, y, size in assembly_labels():
        add_text(board, text, x, y, size)
    add_text(board, "REV A", 155, 136, 1.0)
    board.BuildConnectivity()
    return board


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
    board = pcbnew.LoadBoard(str(board_path))
    manifest_nets = manifest["nets"]
    details = manifest["component_details"]
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
        position = mounting_holes[reference].GetPosition()
        if (
            abs(pcbnew.ToMM(position.x) - expected_x) > 0.01
            or abs(pcbnew.ToMM(position.y) - expected_y) > 0.01
        ):
            raise SystemExit(f"{reference} mounting-hole placement is stale")

    actual: dict[str, str] = {}
    placements = placement_table()
    for reference, footprint in footprints.items():
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
        or board.GetLayerType(pcbnew.In2_Cu) != pcbnew.LT_SIGNAL
    ):
        raise SystemExit("PCB internal-layer types are stale")
    critical_layers: dict[str, set[int]] = {
        "PICO_CLK": set(),
        "D4": set(),
    }
    critical_vias = {"PICO_CLK": 0, "D4": 0}
    locked_z80_clock_segments = 0
    for item in board.GetTracks():
        net_name = str(item.GetNetname())
        if item.GetLayer() == pcbnew.In1_Cu:
            raise SystemExit("In1.Cu GND plane contains a signal track")
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
    if critical_layers["D4"] != {pcbnew.In2_Cu} or \
            critical_vias["D4"] != 0:
        raise SystemExit("D4 must remain a via-free In2.Cu route")
    if locked_z80_clock_segments != 4:
        raise SystemExit(
            "Z80_CLK CPU path must retain four locked F.Cu segments"
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
    expected_zone_outline = tuple((
        (BOARD_LEFT + 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_BOTTOM - 0.5),
        (BOARD_LEFT + 0.5, BOARD_BOTTOM - 0.5),
    ))
    matching_zones = [
        zone for zone in board.Zones()
        if str(zone.GetZoneName()) == "INNER_GROUND_PLANE"
    ]
    if (
        len(matching_zones) != 1
        or str(matching_zones[0].GetNetname()) != "GND"
        or matching_zones[0].GetLayer() != pcbnew.In1_Cu
        or matching_zones[0].GetPadConnection() !=
           pcbnew.ZONE_CONNECTION_THERMAL
        or not close_mm(matching_zones[0].GetLocalClearance(), 0.30)
        or not close_mm(matching_zones[0].GetMinThickness(), 0.25)
        or not matching_zones[0].IsFilled()
        or not matching_zones[0].HasFilledPolysForLayer(pcbnew.In1_Cu)
    ):
        raise SystemExit("PCB must contain one filled INNER_GROUND_PLANE")
    actual_zone_outline = tuple(
        (round(pcbnew.ToMM(point.x), 2),
         round(pcbnew.ToMM(point.y), 2))
        for point in matching_zones[0].Outline().COutline(0).CPoints()
    )
    if actual_zone_outline != expected_zone_outline:
        raise SystemExit("INNER_GROUND_PLANE outline is stale")
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
        pcbnew.ToMM(antenna_box.GetLeft()) < ANTENNA_LEFT
        or pcbnew.ToMM(antenna_box.GetRight()) > ANTENNA_RIGHT
        or pcbnew.ToMM(antenna_box.GetTop()) < ANTENNA_TOP
        or pcbnew.ToMM(antenna_box.GetBottom()) > ANTENNA_BOTTOM
    ):
        raise SystemExit("Pico antenna keepout is not contained by cutout")
    expected_labels = {
        (text, round(x, 2), round(y, 2))
        for text, x, y, _ in assembly_labels()
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
    for reference in [f"C{index}" for index in range(1, 9)] + ["A1"]:
        if any(
            item.GetLayer() == pcbnew.F_SilkS
            for item in footprints[reference].GraphicalItems()
        ):
            raise SystemExit(f"{reference} body graphics must remain on F.Fab")
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
    args.dsn.write_text(
        f"(pcb {DSN_PATH.name}\n{remainder}",
        encoding="utf-8",
    )
    if args.session is not None:
        if not pcbnew.ImportSpecctraSES(board, str(args.session)):
            raise SystemExit(f"could not import routing session {args.session}")
        board.BuildConnectivity()
        add_ground_zone(board, board.FindNet("GND"))
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not pcbnew.SaveBoard(str(args.output), board):
        raise SystemExit(f"could not write {args.output}")
    if args.output.resolve() == BOARD_PATH.resolve():
        sync_project_rules()
    print(f"Wrote {args.output}")
    print(f"Wrote {args.dsn}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
