#!/usr/bin/env python3
"""Compare KiCad's exported XML netlist with the independent manifest."""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("netlist", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))["nets"]
    expected = {
        name: sorted(pin for pin in pins if not pin.startswith("#FLG"))
        for name, pins in manifest.items()
    }

    root = ET.parse(args.netlist).getroot()
    actual: dict[str, list[str]] = {}
    for net in root.findall(".//nets/net"):
        name = net.attrib["name"].removeprefix("/")
        if name.startswith("unconnected-"):
            continue
        actual[name] = sorted(
            f"{node.attrib['ref']}.{node.attrib['pin']}"
            for node in net.findall("node")
        )

    if set(expected) != set(actual):
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        raise SystemExit(f"net names differ: missing={missing}, extra={extra}")

    for name, endpoints in expected.items():
        if endpoints != actual[name]:
            missing = sorted(set(endpoints) - set(actual[name]))
            extra = sorted(set(actual[name]) - set(endpoints))
            raise SystemExit(
                f"net {name} differs: missing endpoints={missing}, extra endpoints={extra}"
            )

    endpoint_count = sum(map(len, actual.values()))
    print(
        f"PASS: {len(actual)} real nets and {endpoint_count} component pin "
        "endpoints exactly match"
    )


if __name__ == "__main__":
    main()