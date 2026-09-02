#!/usr/bin/env python3
"""Verify documented chip-pair Mermaid edges against the KiCad net manifest."""

from __future__ import annotations

import json
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = REPO_ROOT / "hardware/kicad/reports/net_manifest.json"
DOC_PATHS = [
    REPO_ROOT / "docs/docs/en/hardware/pin-mapping.md",
    REPO_ROOT / "docs/docs/en/hardware/address-interface.md",
    REPO_ROOT / "docs/docs/en/hardware/output-buffer.md",
    REPO_ROOT / "docs/docs/en/hardware/bus-isolation.md",
]
AGGREGATE_HEADINGS = {"Memory Control", "Power and Fixed Pins"}
FIXED_ENDPOINT_NETS = {
    "U1.11": "+5V", "U1.16": "INT_N", "U1.17": "NMI_N", "U1.29": "GND",
    "U2.2": "GND", "U2.16": "GND", "U2.30": "+5V", "U2.32": "+5V",
    "U3.12": "GND", "U3.24": "+5V",
    "U4.1": "GND", "U4.10": "GND", "U4.19": "GND", "U4.20": "+5V",
    "U7.1": "GND", "U7.10": "GND", "U7.13": "GND", "U7.15": "GND",
    "U7.17": "GND", "U7.19": "GND", "U7.20": "+3V3",
    "U8.9": "+5V", "U8.10": "GND", "U8.15": "GND", "U8.16": "GND",
    "U8.17": "GND",
    "U9.1": "+5V", "U9.10": "GND", "U9.19": "DATA_UP_OE_N", "U9.20": "+5V",
    "U10.1": "GND", "U10.10": "GND", "U10.19": "DATA_DOWN_OE_N", "U10.20": "+3V3",
    "Q1.1": "GND", "Q1.2": "MCP_RESET_BASE", "Q1.3": "MCP_RESET_N",
    "D1.1": "VSYS", "D1.2": "+5V",
}
DOCUMENTED_NO_CONNECTS = {
    "U1.18", "U1.28", "U2.1", "U3.21", "U3.22", "U3.23",
    "U7.3", "U7.5", "U7.7", "U8.19", "U8.20",
    "A1.11", "A1.30", "A1.35", "A1.37", "A1.40",
}
DEVICE_REFS = [
    ("SN74AHCT244", "U4"), ("AHCT244", "U4"),
    ("SN74AHCT245", "U9"), ("AHCT245", "U9"),
    ("SN74LVC245", "U10"), ("LVC245", "U10"),
    ("SN74LVC244", "U7"), ("LVC244", "U7"),
    ("ATF22V10", "U3"), ("MCP23S17", "U8"), ("MCP ", "U8"),
    ("Z84C00", "U1"), ("Z80", "U1"), ("SRAM", "U2"), ("Pico", "A1"),
]
PICO_HEADER_BY_GP = {
    0: 1, 1: 2, 2: 4, 3: 5, 4: 6, 5: 7, 6: 9, 7: 10, 8: 11,
    9: 12, 10: 14, 11: 15, 12: 16, 13: 17, 14: 19, 15: 20,
    16: 21, 17: 22, 18: 24, 19: 25, 20: 26, 21: 27, 22: 29,
    26: 31, 27: 32, 28: 34,
}
NODE_RE = re.compile(r'([A-Za-z][A-Za-z0-9_]*)\["(.*?)"\]')
EDGE_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*(?:-->|<-->)\s*([A-Za-z][A-Za-z0-9_]*)\s*$")
HEADING_RE = re.compile(r"^#{2,3}\s+(.+?)\s*$")


def endpoint_for(label: str) -> str | None:
    text = label.replace("<br/>", " ")
    if text == "External regulated +5 V":
        return "J1.1"
    if text.startswith("1N5819"):
        return "D1"
    if text.startswith("Q1 base"):
        return "Q1.2"
    if text.startswith("Q1 collector"):
        return "Q1.3"

    reference = next((ref for name, ref in DEVICE_REFS if name in text), None)
    if reference is None:
        return None

    header_match = re.search(r"header pin\s+(\d+)", text)
    if reference == "A1" and header_match:
        return f"A1.{header_match.group(1)}"
    if reference == "A1":
        gp_match = re.search(r"\bGP(\d+)\b", text)
        if gp_match:
            gpio = int(gp_match.group(1))
            if gpio not in PICO_HEADER_BY_GP:
                raise ValueError(f"unsupported Pico GPIO in label: {label}")
            return f"A1.{PICO_HEADER_BY_GP[gpio]}"

    pin_match = re.search(r"\bpin\s+(\d+)\b", text)
    if pin_match:
        return f"{reference}.{pin_match.group(1)}"
    return None


def mermaid_blocks(path: Path) -> list[tuple[str, int, str]]:
    blocks: list[tuple[str, int, str]] = []
    heading = ""
    lines = path.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        heading_match = HEADING_RE.match(lines[index])
        if heading_match:
            heading = heading_match.group(1)
        if lines[index].strip() != "```mermaid":
            index += 1
            continue
        start_line = index + 1
        index += 1
        body: list[str] = []
        while index < len(lines) and lines[index].strip() != "```":
            body.append(lines[index])
            index += 1
        blocks.append((heading, start_line, "\n".join(body)))
        index += 1
    return blocks


def main() -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))["nets"]
    endpoint_net = {
        endpoint: net
        for net, endpoints in manifest.items()
        for endpoint in endpoints
        if not endpoint.startswith("#FLG")
    }
    errors: list[str] = []
    checked_edges = 0
    checked_blocks = 0
    aggregate_blocks: list[str] = []

    for endpoint, expected_net in FIXED_ENDPOINT_NETS.items():
        actual_net = endpoint_net.get(endpoint)
        if actual_net != expected_net:
            errors.append(
                f"fixed endpoint {endpoint}: expected {expected_net}, got {actual_net}"
            )
    for endpoint in DOCUMENTED_NO_CONNECTS:
        if endpoint in endpoint_net:
            errors.append(
                f"documented no-connect {endpoint} unexpectedly joins {endpoint_net[endpoint]}"
            )

    for path in DOC_PATHS:
        for heading, line, body in mermaid_blocks(path):
            if not body.startswith("block-beta"):
                continue
            location = f"{path.relative_to(REPO_ROOT)}:{line} ({heading})"
            if heading in AGGREGATE_HEADINGS:
                aggregate_blocks.append(location)
                continue

            nodes = dict(NODE_RE.findall(body))
            edges = [EDGE_RE.match(item) for item in body.splitlines()]
            edges = [match.groups() for match in edges if match]
            if not edges:
                errors.append(f"{location}: no chip-pair edges found")
                continue
            checked_blocks += 1

            for left_id, right_id in edges:
                left_label = nodes.get(left_id)
                right_label = nodes.get(right_id)
                if left_label is None or right_label is None:
                    errors.append(f"{location}: undefined edge node {left_id} -> {right_id}")
                    continue
                try:
                    left = endpoint_for(left_label)
                    right = endpoint_for(right_label)
                except ValueError as error:
                    errors.append(f"{location}: {error}")
                    continue
                if left is None or right is None:
                    errors.append(
                        f"{location}: cannot resolve {left_label!r} -> {right_label!r}"
                    )
                    continue

                if left == "D1" or right == "D1":
                    other = right if left == "D1" else left
                    diode_pin = "D1.2" if endpoint_net.get(other) == "+5V" else "D1.1"
                    left = diode_pin if left == "D1" else left
                    right = diode_pin if right == "D1" else right

                if {left, right} == {"U3.19", "Q1.2"}:
                    series_ok = (
                        endpoint_net.get("U3.19") == endpoint_net.get("R29.1")
                        and endpoint_net.get("R29.2") == endpoint_net.get("Q1.2")
                    )
                    if not series_ok:
                        errors.append(f"{location}: U3.19 -> R29 -> Q1.2 series path differs")
                    else:
                        checked_edges += 1
                    continue

                if left not in endpoint_net or right not in endpoint_net:
                    missing = [item for item in (left, right) if item not in endpoint_net]
                    errors.append(f"{location}: endpoint absent from manifest: {missing}")
                    continue
                if endpoint_net[left] != endpoint_net[right]:
                    errors.append(
                        f"{location}: {left_label!r} ({left}={endpoint_net[left]}) != "
                        f"{right_label!r} ({right}={endpoint_net[right]})"
                    )
                    continue
                checked_edges += 1

    if errors:
        raise SystemExit("Documentation interconnect audit failed:\n- " + "\n- ".join(errors))

    print(
        f"PASS: {checked_edges} chip-pair edges in {checked_blocks} diagrams "
        "match KiCad net membership"
    )
    print(
        f"PASS: {len(FIXED_ENDPOINT_NETS)} fixed endpoints and "
        f"{len(DOCUMENTED_NO_CONNECTS)} no-connects match KiCad"
    )
    print(
        "INFO: redundant aggregate views excluded from edge counting: "
        + ", ".join(aggregate_blocks)
    )


if __name__ == "__main__":
    main()
