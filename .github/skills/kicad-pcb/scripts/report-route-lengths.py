#!/usr/bin/env python3
"""Report routed copper lengths for timing-sensitive PCB nets."""

from __future__ import annotations

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


def mm(value: int) -> float:
    return pcbnew.ToMM(value)


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
    starts = [node for node in graph if node[:2] == start]
    finishes = {node for node in graph if node[:2] == finish}
    if not starts or not finishes:
        return float("inf")
    queue = [(0.0, node) for node in starts]
    distances = {node: 0.0 for node in starts}
    heapq.heapify(queue)
    while queue:
        distance, node = heapq.heappop(queue)
        if node in finishes:
            return distance
        if distance != distances[node]:
            continue
        for neighbor, weight in graph.get(node, []):
            candidate = distance + weight
            if candidate < distances.get(neighbor, float("inf")):
                distances[neighbor] = candidate
                heapq.heappush(queue, (candidate, neighbor))
    return float("inf")


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
            if distance != float("inf") and distance > longest:
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
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
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
    print("path,length_mm")
    for label, net_name, left_ref, left_pin, right_ref, right_pin in (
        ("Pico clock A1.4->U4.2", "PICO_CLK", "A1", "4", "U4", "2"),
        ("Z80 clock U4.18->U1.6", "Z80_CLK", "U4", "18", "U1", "6"),
        ("Z80 clock U4.18->TP2.1", "Z80_CLK", "U4", "18", "TP2", "1"),
        ("SRAM CE U4.3->U2.22", "SRAM_CE_N", "U4", "3", "U2", "22"),
        ("SRAM OE U4.5->U2.24", "SRAM_OE_N", "U4", "5", "U2", "24"),
        ("SRAM WE U4.7->U2.29", "SRAM_WE_N", "U4", "7", "U2", "29"),
    ):
        graph, _, _ = graph_for_net(board, net_name)
        left = point(
            board.FindFootprintByReference(left_ref).FindPadByNumber(left_pin)
        )
        right = point(
            board.FindFootprintByReference(right_ref).FindPadByNumber(right_pin)
        )
        print(f"{label},{shortest(graph, left, right):.1f}")

    def report_endpoint_paths(
        title: str,
        nets: list[str],
        source_reference: str,
        destination_reference: str,
    ) -> None:
        print(f"\n{title}")
        print("net,length_mm")
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
            print(
                f"{net_name},"
                f"{shortest(graph, source_position, destination_position):.1f}"
            )

    address_nets = [f"A{bit}" for bit in range(16)]
    data_nets = [f"D{bit}" for bit in range(8)]
    report_endpoint_paths(
        "Z80-to-SRAM address paths", address_nets, "U1", "U2"
    )
    report_endpoint_paths(
        "MCP23S17-to-SRAM address paths", address_nets, "U8", "U2"
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


if __name__ == "__main__":
    main()
