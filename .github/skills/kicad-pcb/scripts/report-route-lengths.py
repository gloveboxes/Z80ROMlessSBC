#!/usr/bin/env python3
"""Report routed copper lengths for timing-sensitive PCB nets."""

from __future__ import annotations

import argparse
import heapq
import json
from pathlib import Path

import wx

_WX_APP = wx.App(False)
import pcbnew  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[4]
BOARD_PATH = REPO_ROOT / "hardware" / "kicad" / "z80_romless_sbc.kicad_pcb"
MANIFEST_PATH = (
    REPO_ROOT / "hardware" / "kicad" / "reports" / "net_manifest.json"
)


PLANE_NETS = {"+5V": "In2.Cu", "GND": "In1.Cu"}


def mm(value: int) -> float:
    return pcbnew.ToMM(value)


def plane_path_mm(
    board: pcbnew.BOARD,
    net_name: str,
    left_pad: pcbnew.PAD,
    right_pad: pcbnew.PAD,
) -> float:
    """Return the pad-to-pad distance across a filled inner plane."""
    layer = board.GetLayerID(PLANE_NETS[net_name])
    if not any(
        zone.GetNetname() == net_name and zone.IsOnLayer(layer)
        and zone.IsFilled()
        for zone in board.Zones()
    ):
        raise ValueError(f"{net_name} has no filled plane on {PLANE_NETS[net_name]}")
    ends = []
    for pad in (left_pad, right_pad):
        reference = f"{pad.GetParentFootprint().GetReference()}.{pad.GetNumber()}"
        if pad.GetNetname() != net_name:
            raise ValueError(f"{reference} is not on {net_name}")
        if pad.IsOnLayer(layer):
            ends.append((pad.GetPosition(), 0.0))
            continue
        # SMD pads reach the plane through their own fanout via.
        vias = [
            item for item in board.GetTracks()
            if item.Type() == pcbnew.PCB_VIA_T
            and item.GetNetname() == net_name and item.IsOnLayer(layer)
        ]
        if not vias:
            raise ValueError(f"{reference} does not reach the {net_name} plane")
        via = min(vias, key=lambda item: (
            item.GetPosition() - pad.GetPosition()
        ).EuclideanNorm())
        stub = mm(int((via.GetPosition() - pad.GetPosition()).EuclideanNorm()))
        if stub > 2.5:
            raise ValueError(f"{reference} has no {net_name} plane via within 2.5 mm")
        ends.append((via.GetPosition(), stub))
    (left, left_stub), (right, right_stub) = ends
    dx = mm(left.x - right.x)
    dy = mm(left.y - right.y)
    return left_stub + (dx * dx + dy * dy) ** 0.5 + right_stub


def point(item: pcbnew.BOARD_ITEM) -> tuple[int, int]:
    position = item.GetPosition()
    return position.x, position.y


def copper_layers(item: pcbnew.BOARD_ITEM) -> list[int]:
    return list(item.GetLayerSet().CuStack())


def graph_for_net(
    board: pcbnew.BOARD,
    net_name: str,
) -> tuple[dict[tuple[int, int, int], list[tuple[tuple[int, int, int], float]]],
           float, int]:
    graph: dict[
        tuple[int, int, int],
        list[tuple[tuple[int, int, int], float]],
    ] = {}
    total = 0.0
    vias = 0

    def connect(
        left: tuple[int, int, int],
        right: tuple[int, int, int],
        weight: float,
    ) -> None:
        graph.setdefault(left, []).append((right, weight))
        graph.setdefault(right, []).append((left, weight))

    for track in board.TracksInNet(board.GetNetcodeFromNetname(net_name)):
        start = track.GetStart()
        end = track.GetEnd()
        if track.Type() == pcbnew.PCB_VIA_T:
            vias += 1
            layers = copper_layers(track)
            for index, layer in enumerate(layers):
                for other in layers[index + 1:]:
                    connect(
                        (start.x, start.y, layer),
                        (start.x, start.y, other),
                        0.0,
                    )
            continue
        length = mm(track.GetLength())
        total += length
        connect(
            (start.x, start.y, track.GetLayer()),
            (end.x, end.y, track.GetLayer()),
            length,
        )

    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if str(pad.GetNetname()) != net_name:
                continue
            x, y = point(pad)
            layers = copper_layers(pad)
            for node in ((x, y, layer) for layer in layers):
                graph.setdefault(node, [])
            for index, layer in enumerate(layers):
                for other in layers[index + 1:]:
                    connect((x, y, layer), (x, y, other), 0.0)
    return graph, total, vias


def shortest(
    graph: dict[
        tuple[int, int, int],
        list[tuple[tuple[int, int, int], float]],
    ],
    start: tuple[int, int],
    finish: tuple[int, int],
) -> float:
    return shortest_metrics(graph, start, finish)[0]


def shortest_metrics(
    graph: dict[
        tuple[int, int, int],
        list[tuple[tuple[int, int, int], float]],
    ],
    start: tuple[int, int],
    finish: tuple[int, int],
) -> tuple[float, int]:
    starts = [node for node in graph if node[:2] == start]
    finishes = {node for node in graph if node[:2] == finish}
    if not starts or not finishes:
        raise ValueError(f"Missing routed endpoint: {start} -> {finish}")
    queue = [(0.0, 0, node) for node in starts]
    distances = {node: (0.0, 0) for node in starts}
    heapq.heapify(queue)
    while queue:
        distance, changes, node = heapq.heappop(queue)
        if node in finishes:
            return distance, changes
        if (distance, changes) != distances[node]:
            continue
        for neighbor, weight in graph.get(node, []):
            candidate = (distance + weight, changes + int(node[2] != neighbor[2]))
            if candidate < distances.get(neighbor, (float("inf"), 0)):
                distances[neighbor] = candidate
                heapq.heappush(queue, (*candidate, neighbor))
    raise ValueError(f"Disconnected routed endpoints: {start} -> {finish}")


def endpoints(
    board: pcbnew.BOARD,
    manifest: dict[str, object],
    net_name: str,
) -> list[tuple[str, tuple[int, int]]]:
    result = []
    for endpoint in manifest["nets"][net_name]:
        reference, number = endpoint.split(".", 1)
        if reference.startswith("#"):
            continue
        footprint = board.FindFootprintByReference(reference)
        if footprint is None:
            continue
        pad = footprint.FindPadByNumber(number)
        if pad is not None:
            result.append((endpoint, point(pad)))
    return result


def net_metrics(
    board: pcbnew.BOARD,
    manifest: dict[str, object],
    net_name: str,
) -> tuple[float, float, str, int]:
    graph, total, via_count = graph_for_net(board, net_name)
    pads = endpoints(board, manifest, net_name)
    longest = 0.0
    pair = ""
    for index, (left_name, left) in enumerate(pads):
        for right_name, right in pads[index + 1:]:
            distance = shortest(graph, left, right)
            if distance > longest:
                longest = distance
                pair = f"{left_name}->{right_name}"
    return total, longest, pair, via_count


def report_group(
    board: pcbnew.BOARD,
    manifest: dict[str, object],
    title: str,
    nets: list[str],
) -> None:
    print(f"\n{title}")
    print("net,total_mm,max_pad_path_mm,vias,max_path")
    totals = []
    longest_paths = []
    for net_name in nets:
        total, longest, pair, vias = net_metrics(board, manifest, net_name)
        totals.append(total)
        longest_paths.append(longest)
        print(f"{net_name},{total:.1f},{longest:.1f},{vias},{pair}")
    print(
        f"summary,total={min(totals):.1f}..{max(totals):.1f},"
        f"max_path={min(longest_paths):.1f}..{max(longest_paths):.1f}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", type=Path, help="write machine-readable path evidence")
    parser.add_argument("--check", action="store_true", help="enforce layout review budgets, not timing qualification")
    parser.add_argument("--board", type=Path, default=BOARD_PATH, help="routed .kicad_pcb to report")
    parser.add_argument("--manifest", type=Path, default=MANIFEST_PATH, help="net_manifest.json for the board")
    args = parser.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    evidence = {"nets": {}, "paths": {}, "groups": {}}
    for net_name in (
        [f"A{bit}" for bit in range(16)] + [f"D{bit}" for bit in range(8)]
        + [f"PICO_D{bit}" for bit in range(8)]
        + ["PICO_CLK", "Z80_CLK", "WAIT_N", "SRAM_CE_N", "SRAM_OE_N", "SRAM_WE_N"]
    ):
        total, longest, pair, vias = net_metrics(board, manifest, net_name)
        evidence["nets"][net_name] = {
            "total_mm": round(total, 3), "max_pad_path_mm": round(longest, 3),
            "max_path": pair, "vias": vias,
        }
    report_group(board, manifest, "Address bus", [f"A{bit}" for bit in range(16)])
    report_group(board, manifest, "Data bus", [f"D{bit}" for bit in range(8)])
    report_group(
        board, manifest, "Pico data bus",
        [f"PICO_D{bit}" for bit in range(8)],
    )
    report_group(
        board, manifest, "Timing and SRAM controls",
        [
            "PICO_CLK", "Z80_CLK", "WAIT_N",
            "SRAM_CE_N", "SRAM_OE_N", "SRAM_WE_N",
        ],
    )
    print("\nCritical endpoint paths")
    print("path,length_mm,layer_changes")
    for label, net_name, left_ref, left_pin, right_ref, right_pin in (
        ("Pico clock A1.4->U4.2", "PICO_CLK", "A1", "4", "U4", "2"),
        ("Z80 clock U4.18->U1.6", "Z80_CLK", "U4", "18", "U1", "6"),
        ("SRAM CE U4.12->U2.22", "SRAM_CE_N", "U4", "12", "U2", "22"),
        ("SRAM OE U3.3->U2.24", "SRAM_OE_N", "U3", "3", "U2", "24"),
        ("SRAM WE U4.9->U2.29", "SRAM_WE_N", "U4", "9", "U2", "29"),
        ("WAIT U3.6->U1.24", "WAIT_N", "U3", "6", "U1", "24"),
        ("IORQ U1.20->U3.4", "IORQ_N", "U1", "20", "U3", "4"),
        ("RD U1.21->U4.13", "RD_N", "U1", "21", "U4", "13"),
        ("Buffered RD U4.7->U3.1", "RD_5V_N", "U4", "7", "U3", "1"),
        ("A16 U4.3->U2.2", "SRAM_A16", "U4", "3", "U2", "2"),
        ("Z80 bypass C1.1->U1.11", "+5V", "C1", "1", "U1", "11"),
        ("SRAM bypass C2.1->U2.32", "+5V", "C2", "1", "U2", "32"),
        ("HCT32 bypass C3.1->U3.14", "+5V", "C3", "1", "U3", "14"),
        ("AHCT244 bypass C4.1->U4.20", "+5V", "C4", "1", "U4", "20"),
        ("LVC244 bypass C5.1->U7.20", "+3V3", "C5", "1", "U7", "20"),
        ("AHCT245 bypass C6.1->U9.20", "+5V", "C6", "1", "U9", "20"),
        ("LVC245 bypass C7.1->U10.20", "+3V3", "C7", "1", "U10", "20"),
    ):
        left_pad = board.FindFootprintByReference(left_ref).FindPadByNumber(left_pin)
        right_pad = board.FindFootprintByReference(right_ref).FindPadByNumber(right_pin)
        left, right = point(left_pad), point(right_pad)
        if net_name in PLANE_NETS:
            distance = plane_path_mm(board, net_name, left_pad, right_pad)
            changes = 0
            label = f"{label} (plane)"
        else:
            graph, _, _ = graph_for_net(board, net_name)
            distance, changes = shortest_metrics(graph, left, right)
        evidence["paths"][label] = {
            "length_mm": round(distance, 3), "layer_changes": changes,
        }
        print(f"{label},{distance:.1f},{changes}")

    def report_endpoint_paths(
        title: str,
        nets: list[str],
        source_reference: str,
        destination_reference: str,
    ) -> None:
        print(f"\n{title}")
        print("net,length_mm,layer_changes")
        group = {}
        for net_name in nets:
            endpoint_names = manifest["nets"][net_name]
            source_endpoint = next(
                endpoint for endpoint in endpoint_names
                if endpoint.startswith(source_reference + ".")
            )
            destination_endpoint = next(
                endpoint for endpoint in endpoint_names
                if endpoint.startswith(destination_reference + ".")
            )
            source_ref, source_pin = source_endpoint.split(".", 1)
            destination_ref, destination_pin = destination_endpoint.split(
                ".", 1
            )
            graph, _, _ = graph_for_net(board, net_name)
            source_position = point(
                board.FindFootprintByReference(
                    source_ref
                ).FindPadByNumber(source_pin)
            )
            destination_position = point(
                board.FindFootprintByReference(
                    destination_ref
                ).FindPadByNumber(destination_pin)
            )
            distance, changes = shortest_metrics(graph, source_position, destination_position)
            group[net_name] = {"length_mm": round(distance, 3), "layer_changes": changes}
            print(f"{net_name},{distance:.1f},{changes}")
        lengths = [entry["length_mm"] for entry in group.values()]
        evidence["groups"][title] = {
            "nets": group, "min_mm": min(lengths), "max_mm": max(lengths),
            "spread_mm": round(max(lengths) - min(lengths), 3),
        }

    address_nets = [f"A{bit}" for bit in range(16)]
    data_nets = [f"D{bit}" for bit in range(8)]
    report_endpoint_paths(
        "Z80-to-SRAM address paths", address_nets, "U1", "U2"
    )
    report_endpoint_paths(
        "Z80-to-SRAM data paths", data_nets, "U1", "U2"
    )
    report_endpoint_paths(
        "Pico-write-transceiver-to-SRAM data paths",
        data_nets,
        "U9",
        "U2",
    )
    report_endpoint_paths(
        "SRAM-to-Pico-read-transceiver data paths",
        data_nets,
        "U2",
        "U10",
    )
    for reference in ("U9", "U10"):
        report_endpoint_paths(
            f"Pico-to-{reference} data paths",
            [f"PICO_D{bit}" for bit in range(8)], "A1", reference,
        )
    if args.check:
        budgets = {
            "Pico clock A1.4->U4.2": 60,
            "Z80 clock U4.18->U1.6": 70,
            "SRAM CE U4.12->U2.22": 60,
            "SRAM OE U3.3->U2.24": 60,
            "SRAM WE U4.9->U2.29": 60,
            "WAIT U3.6->U1.24": 60,
            "RD U1.21->U4.13": 60,
        }
        for label, metrics in evidence["paths"].items():
            limit = 8 if "bypass" in label else budgets.get(label)
            if limit is not None and metrics["length_mm"] > limit:
                raise ValueError(f"{label} exceeds {limit} mm layout budget: {metrics}")
        for title, metrics in evidence["groups"].items():
            if metrics["max_mm"] > 100:
                raise ValueError(f"{title} exceeds 100 mm source-path layout budget")
        for net_name in ("PICO_CLK", "Z80_CLK"):
            if evidence["nets"][net_name]["vias"] != 0:
                raise ValueError(f"{net_name} must remain via-free")
        print("\nPASS: routed paths satisfy layout review budgets (not timing qualification)")
    if args.json is not None:
        args.json.write_text(json.dumps(evidence, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
