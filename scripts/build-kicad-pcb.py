#!/usr/bin/env python3
"""Generate the placed two-layer PCB from the schematic net manifest.

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
BOARD_RIGHT = 185.0
BOARD_BOTTOM = 140.0
ANTENNA_LEFT = 156.4
ANTENNA_RIGHT = 171.4
ANTENNA_TOP = 128.5
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
COPPER_EDGE_CLEARANCE = 0.50


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
        "D1": (139, 84, 0),
        "U2": (10, 22, 0),
        "U1": (40, 17, 0),
        "U8": (78, 22, 0),
        "Q1": (80, 61, 0),
        "U3": (94, 22, 0),
        "U4": (60, 20, 0),
        "U9": (105, 55, 0),
        "U10": (124, 55, 0),
        "U7": (143, 55, 0),
        "A1": (155, 88, 0),
        "RN1": (8, 67, 0),
        "RN2": (34, 72, 0),
        "RN3": (148, 112, 90),
        "C1": (34, 42.5, 0),
        "C2": (28, 22, 0),
        "C3": (105, 30, 0),
        "C4": (62, 15, 0),
        "C5": (155, 55, 90),
        "C6": (71, 43, 0),
        "C7": (115.2, 55, 90),
        "C8": (134.2, 55, 90),
        "C9": (32, 61, 0),
        "C10": (61, 72, 0),
        "C11": (178, 50, 0),
        "C12": (27, 12, 0),
        "R29": (90, 135, 90),
        "R30": (97, 135, 90),
        "R31": (104, 135, 90),
        "TP1": (43, 10, 0),
        "TP2": (60, 10, 0),
        "TP3": (50, 10, 0),
        "TP4": (74, 10, 0),
        "TP5": (81, 10, 0),
        "TP6": (88, 10, 0),
        "TP7": (95, 10, 0),
    }
    for index in range(1, 17):
        placements[f"R{index}"] = (8 + (index - 1) * 5.6, 90, 90)
    for index in range(17, 23):
        placements[f"R{index}"] = (95 + (index - 17) * 8, 98, 90)
    for index in range(23, 29):
        placements[f"R{index}"] = (95 + (index - 23) * 8, 115, 90)
    return placements


def mounting_hole_table() -> dict[str, tuple[float, float]]:
    return {
        "H1": (9, 9),
        "H2": (181, 9),
        "H3": (9, 136),
        "H4": (135, 136),
    }


def assembly_labels() -> list[tuple[str, float, float, float]]:
    return [
        ("C1", 31.0, 42.5, 0.8),
        ("C2", 31.0, 26.0, 0.8),
        ("C3", 105.0, 35.0, 0.8),
        ("C4", 67.0, 15.0, 0.8),
        ("C5", 158.0, 55.0, 0.8),
        ("C6", 68.0, 47.0, 0.8),
        ("C7", 117.5, 60.0, 0.8),
        ("C8", 136.5, 60.0, 0.8),
        ("C9", 37.0, 61.0, 0.8),
        ("C10", 61.0, 78.0, 0.8),
        ("C11", 178.0, 44.0, 0.8),
        ("C12", 27.0, 19.0, 0.8),
        ("D1", 145.0, 88.0, 0.8),
        ("A1 PICO 2 W", 163.9, 124.0, 1.0),
        ("USB CABLE TO BOARD TOP", 164.0, 62.0, 0.8),
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
    points = [
        (BOARD_LEFT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_TOP),
        (BOARD_RIGHT, BOARD_BOTTOM),
        (ANTENNA_RIGHT, BOARD_BOTTOM),
        (ANTENNA_RIGHT, ANTENNA_TOP),
        (ANTENNA_LEFT, ANTENNA_TOP),
        (ANTENNA_LEFT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_BOTTOM),
        (BOARD_LEFT, BOARD_TOP),
    ]
    for start, end in zip(points, points[1:]):
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
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetNet(ground)
    zone.SetZoneName("BOTTOM_GROUND_PLANE")
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    zone.SetLocalClearance(mm(0.3))
    zone.SetMinThickness(mm(0.25))
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in (
        (BOARD_LEFT + 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_BOTTOM - 0.5),
        (ANTENNA_RIGHT + 0.5, BOARD_BOTTOM - 0.5),
        (ANTENNA_RIGHT + 0.5, ANTENNA_TOP - 0.5),
        (ANTENNA_LEFT - 0.5, ANTENNA_TOP - 0.5),
        (ANTENNA_LEFT - 0.5, BOARD_BOTTOM - 0.5),
        (BOARD_LEFT + 0.5, BOARD_BOTTOM - 0.5),
    ):
        outline.Append(mm(x), mm(y))
    board.Add(zone)


def build_board(
    manifest: dict[str, object],
    footprint_root: Path,
) -> pcbnew.BOARD:
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
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
            if endpoint in {
                "A1.3", "A1.13", "C6.2", "C10.2", "C11.2",
                "Q1.1", "R27.2", "R30.2", "U1.29", "U3.12",
                "U8.15",
            }:
                pad.SetLocalZoneConnection(
                    pcbnew.ZONE_CONNECTION_FULL
                )

    add_mounting_holes(board, footprint_root)
    add_outline(board)
    add_text(board, "Z80 ROMless SBC", 130, 9, 2.0)
    for text, x, y, size in assembly_labels():
        add_text(board, text, x, y, size)
    add_text(board, "REV A", 125, 136, 1.0)
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
            signatures.add((
                "via", net_name,
                round(pcbnew.ToMM(position.x), 4),
                round(pcbnew.ToMM(position.y), 4),
                round(pcbnew.ToMM(via.GetWidth(pcbnew.F_Cu)), 4),
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


def polygon_signature(polyset: pcbnew.SHAPE_POLY_SET) -> tuple[object, ...]:
    outlines = []
    for index in range(polyset.OutlineCount()):
        outline = polyset.COutline(index)
        outlines.append(tuple(
            (round(pcbnew.ToMM(point.x), 4),
             round(pcbnew.ToMM(point.y), 4))
            for point in outline.CPoints()
        ))
    return tuple(sorted(outlines))


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
    if board.GetCopperLayerCount() != 2:
        raise SystemExit(
            f"PCB must have exactly two copper layers, got "
            f"{board.GetCopperLayerCount()}"
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
    expected_outline = {
        tuple(sorted((start, end)))
        for start, end in zip(
            [
                (BOARD_LEFT, BOARD_TOP),
                (BOARD_RIGHT, BOARD_TOP),
                (BOARD_RIGHT, BOARD_BOTTOM),
                (ANTENNA_RIGHT, BOARD_BOTTOM),
                (ANTENNA_RIGHT, ANTENNA_TOP),
                (ANTENNA_LEFT, ANTENNA_TOP),
                (ANTENNA_LEFT, BOARD_BOTTOM),
                (BOARD_LEFT, BOARD_BOTTOM),
                (BOARD_LEFT, BOARD_TOP),
            ],
            [
                (BOARD_RIGHT, BOARD_TOP),
                (BOARD_RIGHT, BOARD_BOTTOM),
                (ANTENNA_RIGHT, BOARD_BOTTOM),
                (ANTENNA_RIGHT, ANTENNA_TOP),
                (ANTENNA_LEFT, ANTENNA_TOP),
                (ANTENNA_LEFT, BOARD_BOTTOM),
                (BOARD_LEFT, BOARD_BOTTOM),
                (BOARD_LEFT, BOARD_TOP),
            ],
        )
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
    ground_zones = [
        zone for zone in board.Zones()
        if str(zone.GetZoneName()) == "BOTTOM_GROUND_PLANE"
    ]
    if (
        len(ground_zones) != 1
        or str(ground_zones[0].GetNetname()) != "GND"
        or ground_zones[0].GetLayer() != pcbnew.B_Cu
        or ground_zones[0].GetPadConnection() !=
           pcbnew.ZONE_CONNECTION_THERMAL
        or not close_mm(ground_zones[0].GetLocalClearance(), 0.30)
        or not close_mm(ground_zones[0].GetMinThickness(), 0.25)
        or not ground_zones[0].IsFilled()
        or not ground_zones[0].HasFilledPolysForLayer(pcbnew.B_Cu)
    ):
        raise SystemExit("PCB must contain one filled B.Cu ground plane")
    expected_zone_outline = tuple((
        (BOARD_LEFT + 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_TOP + 0.5),
        (BOARD_RIGHT - 0.5, BOARD_BOTTOM - 0.5),
        (ANTENNA_RIGHT + 0.5, BOARD_BOTTOM - 0.5),
        (ANTENNA_RIGHT + 0.5, ANTENNA_TOP - 0.5),
        (ANTENNA_LEFT - 0.5, ANTENNA_TOP - 0.5),
        (ANTENNA_LEFT - 0.5, BOARD_BOTTOM - 0.5),
        (BOARD_LEFT + 0.5, BOARD_BOTTOM - 0.5),
    ))
    actual_zone_outline = tuple(
        (round(pcbnew.ToMM(point.x), 2),
         round(pcbnew.ToMM(point.y), 2))
        for point in ground_zones[0].Outline().COutline(0).CPoints()
    )
    if actual_zone_outline != expected_zone_outline:
        raise SystemExit("B.Cu ground-plane outline is stale")
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
    antenna_box = antenna_zones[0].GetBoundingBox()
    if (
        pcbnew.ToMM(antenna_box.GetLeft()) < ANTENNA_LEFT
        or pcbnew.ToMM(antenna_box.GetRight()) > ANTENNA_RIGHT
        or pcbnew.ToMM(antenna_box.GetTop()) < ANTENNA_TOP
        or pcbnew.ToMM(antenna_box.GetBottom()) > BOARD_BOTTOM
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
    fill_before = polygon_signature(
        ground_zones[0].GetFilledPolysList(pcbnew.B_Cu)
    )
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    fill_after = polygon_signature(
        ground_zones[0].GetFilledPolysList(pcbnew.B_Cu)
    )
    if fill_before != fill_after:
        raise SystemExit("committed B.Cu ground-plane fill is stale")
    if not SESSION_PATH.is_file():
        raise SystemExit(f"missing routing session {SESSION_PATH}")
    session_board = build_board(manifest, footprint_root)
    if not pcbnew.ImportSpecctraSES(session_board, str(SESSION_PATH)):
        raise SystemExit(f"could not import routing session {SESSION_PATH}")
    if track_signatures(session_board) != track_signatures(board):
        raise SystemExit("committed routing session does not reproduce PCB copper")
    print(
        f"PASS: PCB has {len(footprints)} schematic footprints, "
        f"{len(actual)} checked endpoints, and two copper layers"
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
    args = parser.parse_args()

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
