#!/usr/bin/env python3
"""Generate the partial-SMT KiCad schematic and exact machine-readable net manifest.

This is the JLCPCB-assembly variant of the Rev B schematic; it writes only to
``hardware/kicad-smt`` and never touches the Rev B design in ``hardware/kicad``.
"""

from __future__ import annotations

import json
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from uuid import UUID, uuid5

from kiutils.items.common import (
    Effects,
    Fill,
    Font,
    PageSettings,
    Position,
    Property,
    Stroke,
    TitleBlock,
)
from kiutils.items.schitems import (
    BusEntry,
    Connection,
    HierarchicalSheetInstance,
    Junction,
    LocalLabel,
    NoConnect,
    SchematicSymbol,
    SymbolProjectInstance,
    SymbolProjectPath,
    Text,
)
from kiutils.items.syitems import SyRect
from kiutils.schematic import Schematic
from kiutils.symbol import Symbol, SymbolLib, SymbolPin


REPO_ROOT = Path(__file__).resolve().parent.parent
KICAD_DIR = REPO_ROOT / "hardware" / "kicad-smt"
SCHEMATIC_PATH = KICAD_DIR / "z80_romless_sbc_smt.kicad_sch"
SYMBOL_PATH = KICAD_DIR / "z80sbc_smt.kicad_sym"
MANIFEST_PATH = KICAD_DIR / "reports" / "net_manifest.json"
PROJECT_NAME = "z80_romless_sbc_smt"
UUID_NAMESPACE = UUID("5c0e6f0e-2f4e-4c8e-9a51-8d3f6a0b7c21")
GRID = 2.54


def uid(name: str) -> str:
    return str(uuid5(UUID_NAMESPACE, name))


def pos(x: float, y: float, angle: float | None = None) -> Position:
    return Position(round(x, 4), round(y, 4), angle)


def effects(size: float = 1.27, hide: bool = False) -> Effects:
    return Effects(font=Font(height=size, width=size), hide=hide)


def property_value(
    key: str,
    value: str,
    x: float = 0,
    y: float = 0,
    *,
    hide: bool = False,
) -> Property:
    return Property(
        key=key,
        value=value,
        position=pos(x, y, 0),
        effects=effects(hide=hide),
    )


@dataclass(frozen=True)
class PinDef:
    number: str
    name: str
    electrical_type: str
    side: str


@dataclass
class SymbolGeometry:
    symbol: Symbol
    pin_positions: dict[str, tuple[float, float, str]]
    half_width: float
    half_height: float


def pin(number: int | str, name: str, electrical_type: str, side: str) -> PinDef:
    return PinDef(str(number), name, electrical_type, side)


SYMBOL_SPECS: dict[str, tuple[str, str, list[PinDef]]] = {
    "Z80_CPU": (
        "U",
        "Z84C0020PEC",
        [
            pin(1, "A11", "tri_state", "right"), pin(2, "A12", "tri_state", "right"),
            pin(3, "A13", "tri_state", "right"), pin(4, "A14", "tri_state", "right"),
            pin(5, "A15", "tri_state", "right"), pin(6, "CLK", "input", "left"),
            pin(7, "D4", "bidirectional", "left"), pin(8, "D3", "bidirectional", "left"),
            pin(9, "D5", "bidirectional", "left"), pin(10, "D6", "bidirectional", "left"),
            pin(11, "VCC", "power_in", "top"), pin(12, "D2", "bidirectional", "left"),
            pin(13, "D7", "bidirectional", "left"), pin(14, "D0", "bidirectional", "left"),
            pin(15, "D1", "bidirectional", "left"), pin(16, "INT#", "input", "left"),
            pin(17, "NMI#", "input", "left"), pin(18, "HALT#", "tri_state", "right"),
            pin(19, "MREQ#", "tri_state", "right"), pin(20, "IORQ#", "tri_state", "right"),
            pin(21, "RD#", "tri_state", "right"), pin(22, "WR#", "tri_state", "right"),
            pin(23, "BUSACK#", "tri_state", "right"), pin(24, "WAIT#", "input", "left"),
            pin(25, "BUSREQ#", "input", "left"), pin(26, "RESET#", "input", "left"),
            pin(27, "M1#", "tri_state", "right"), pin(28, "RFSH#", "tri_state", "right"),
            pin(29, "GND", "power_in", "bottom"), pin(30, "A0", "tri_state", "right"),
            pin(31, "A1", "tri_state", "right"), pin(32, "A2", "tri_state", "right"),
            pin(33, "A3", "tri_state", "right"), pin(34, "A4", "tri_state", "right"),
            pin(35, "A5", "tri_state", "right"), pin(36, "A6", "tri_state", "right"),
            pin(37, "A7", "tri_state", "right"), pin(38, "A8", "tri_state", "right"),
            pin(39, "A9", "tri_state", "right"), pin(40, "A10", "tri_state", "right"),
        ],
    ),
    "SRAM": (
        "U",
        "IS62C1024AL-35QLI",
        [
            pin(1, "NC", "no_connect", "left"), pin(2, "A16", "input", "left"),
            pin(3, "A14", "input", "left"), pin(4, "A12", "input", "left"),
            pin(5, "A7", "input", "left"), pin(6, "A6", "input", "left"),
            pin(7, "A5", "input", "left"), pin(8, "A4", "input", "left"),
            pin(9, "A3", "input", "left"), pin(10, "A2", "input", "left"),
            pin(11, "A1", "input", "left"), pin(12, "A0", "input", "left"),
            pin(13, "D0", "bidirectional", "right"), pin(14, "D1", "bidirectional", "right"),
            pin(15, "D2", "bidirectional", "right"), pin(16, "GND", "power_in", "bottom"),
            pin(17, "D3", "bidirectional", "right"), pin(18, "D4", "bidirectional", "right"),
            pin(19, "D5", "bidirectional", "right"), pin(20, "D6", "bidirectional", "right"),
            pin(21, "D7", "bidirectional", "right"), pin(22, "CE#", "input", "right"),
            pin(23, "A10", "input", "left"), pin(24, "OE#", "input", "right"),
            pin(25, "A11", "input", "left"), pin(26, "A9", "input", "left"),
            pin(27, "A8", "input", "left"), pin(28, "A13", "input", "left"),
            pin(29, "WE#", "input", "right"), pin(30, "CE2", "input", "right"),
            pin(31, "A15", "input", "left"), pin(32, "VCC", "power_in", "top"),
        ],
    ),
    "HCT32": (
        "U",
        "SN74HCT32DR",
        [
            pin(1, "1A", "input", "left"), pin(2, "1B", "input", "left"),
            pin(3, "1Y", "output", "right"), pin(4, "2A", "input", "left"),
            pin(5, "2B", "input", "left"), pin(6, "2Y", "output", "right"),
            pin(7, "GND", "power_in", "bottom"), pin(8, "3Y", "output", "right"),
            pin(9, "3A", "input", "left"), pin(10, "3B", "input", "left"),
            pin(11, "4Y", "output", "right"), pin(12, "4A", "input", "left"),
            pin(13, "4B", "input", "left"), pin(14, "VCC", "power_in", "top"),
        ],
    ),
    "AHCT244": (
        "U",
        "SN74AHCT244DWR",
        [
            pin(1, "OE1#", "input", "left"),
            pin(2, "1A1", "input", "left"), pin(3, "2Y4", "tri_state", "right"),
            pin(4, "1A2", "input", "left"), pin(5, "2Y3", "tri_state", "right"),
            pin(6, "1A3", "input", "left"), pin(7, "2Y2", "tri_state", "right"),
            pin(8, "1A4", "input", "left"), pin(9, "2Y1", "tri_state", "right"),
            pin(10, "GND", "power_in", "bottom"),
            pin(11, "2A1", "input", "left"), pin(12, "1Y4", "tri_state", "right"),
            pin(13, "2A2", "input", "left"), pin(14, "1Y3", "tri_state", "right"),
            pin(15, "2A3", "input", "left"), pin(16, "1Y2", "tri_state", "right"),
            pin(17, "2A4", "input", "left"), pin(18, "1Y1", "tri_state", "right"),
            pin(19, "OE2#", "input", "left"), pin(20, "VCC", "power_in", "top"),
        ],
    ),
    "AHCT245": (
        "U",
        "SN74AHCT245DWR",
        [
            pin(1, "DIR", "input", "left"),
            *[pin(number + 1, f"A{number}", "bidirectional", "left") for number in range(1, 9)],
            pin(10, "GND", "power_in", "bottom"),
            *[pin(19 - number, f"B{number}", "bidirectional", "right") for number in range(1, 9)],
            pin(19, "OE#", "input", "left"), pin(20, "VCC", "power_in", "top"),
        ],
    ),
    "LVC245": (
        "U",
        "SN74LVC245ADWR",
        [
            pin(1, "DIR", "input", "left"),
            *[pin(number + 1, f"A{number}", "bidirectional", "left") for number in range(1, 9)],
            pin(10, "GND", "power_in", "bottom"),
            *[pin(19 - number, f"B{number}", "bidirectional", "right") for number in range(1, 9)],
            pin(19, "OE#", "input", "left"), pin(20, "VCC", "power_in", "top"),
        ],
    ),
    "POWER_HEADER": (
        "J", "EXTERNAL 5V",
        [pin(1, "+5V", "passive", "left"), pin(2, "GND", "passive", "left")],
    ),
    "LVC244": (
        "U",
        "SN74LVC244ADWR",
        [
            pin(1, "1OE#", "input", "left"), pin(2, "1A1", "input", "left"),
            pin(3, "2Y4", "tri_state", "right"), pin(4, "1A2", "input", "left"),
            pin(5, "2Y3", "tri_state", "right"), pin(6, "1A3", "input", "left"),
            pin(7, "2Y2", "tri_state", "right"), pin(8, "1A4", "input", "left"),
            pin(9, "2Y1", "tri_state", "right"), pin(10, "GND", "power_in", "bottom"),
            pin(11, "2A1", "input", "left"), pin(12, "1Y4", "tri_state", "right"),
            pin(13, "2A2", "input", "left"), pin(14, "1Y3", "tri_state", "right"),
            pin(15, "2A3", "input", "left"), pin(16, "1Y2", "tri_state", "right"),
            pin(17, "2A4", "input", "left"), pin(18, "1Y1", "tri_state", "right"),
            pin(19, "2OE#", "input", "left"), pin(20, "VCC", "power_in", "top"),
        ],
    ),
    "PICO2": (
        "A",
        "Raspberry Pi Pico 2",
        [
            pin(1, "GP0", "bidirectional", "left"), pin(2, "GP1", "bidirectional", "left"),
            pin(3, "GND1", "power_in", "left"), pin(4, "GP2", "bidirectional", "left"),
            pin(5, "GP3", "bidirectional", "left"), pin(6, "GP4", "bidirectional", "left"),
            pin(7, "GP5", "bidirectional", "left"), pin(8, "GND2", "power_in", "left"),
            pin(9, "GP6", "bidirectional", "left"), pin(10, "GP7", "bidirectional", "left"),
            pin(11, "GP8", "bidirectional", "left"), pin(12, "GP9", "bidirectional", "left"),
            pin(13, "GND3", "power_in", "left"), pin(14, "GP10", "bidirectional", "left"),
            pin(15, "GP11", "bidirectional", "left"), pin(16, "GP12", "bidirectional", "left"),
            pin(17, "GP13", "bidirectional", "left"), pin(18, "GND4", "power_in", "left"),
            pin(19, "GP14", "bidirectional", "left"), pin(20, "GP15", "bidirectional", "left"),
            pin(21, "GP16", "bidirectional", "right"), pin(22, "GP17", "bidirectional", "right"),
            pin(23, "GND5", "power_in", "right"), pin(24, "GP18", "bidirectional", "right"),
            pin(25, "GP19", "bidirectional", "right"), pin(26, "GP20", "bidirectional", "right"),
            pin(27, "GP21", "bidirectional", "right"), pin(28, "GND6", "power_in", "right"),
            pin(29, "GP22", "bidirectional", "right"), pin(30, "RUN", "input", "right"),
            pin(31, "GP26", "bidirectional", "right"), pin(32, "GP27", "bidirectional", "right"),
            pin(33, "AGND", "power_in", "right"), pin(34, "GP28", "bidirectional", "right"),
            pin(35, "ADC_VREF", "power_in", "right"), pin(36, "3V3_OUT", "power_out", "right"),
            pin(37, "3V3_EN", "input", "right"), pin(38, "GND7", "power_in", "right"),
            pin(39, "VSYS", "power_in", "right"), pin(40, "VBUS", "power_in", "right"),
        ],
    ),
    "RESISTOR": (
        "R", "10k", [pin(1, "1", "passive", "left"), pin(2, "2", "passive", "right")]
    ),
    "CAPACITOR": (
        "C", "C", [pin(1, "+", "passive", "top"), pin(2, "-", "passive", "bottom")]
    ),
    "DIODE": (
        "D", "SS14", [pin(1, "K", "passive", "right"), pin(2, "A", "passive", "left")]
    ),
    "USB_C_POWER": (
        "J",
        "USB-C 16P power receptacle",
        [
            pin("A5", "CC1", "passive", "left"),
            pin("B5", "CC2", "passive", "left"),
            pin("A6", "DP1", "passive", "left"),
            pin("A7", "DN1", "passive", "left"),
            pin("B6", "DP2", "passive", "left"),
            pin("B7", "DN2", "passive", "left"),
            pin("A8", "SBU1", "passive", "left"),
            pin("B8", "SBU2", "passive", "left"),
            pin("A4", "VBUS", "passive", "right"),
            pin("A9", "VBUS", "passive", "right"),
            pin("B4", "VBUS", "passive", "right"),
            pin("B9", "VBUS", "passive", "right"),
            pin("A1", "GND", "passive", "right"),
            pin("A12", "GND", "passive", "right"),
            pin("B1", "GND", "passive", "right"),
            pin("B12", "GND", "passive", "right"),
            pin("SH", "SHIELD", "passive", "right"),
        ],
    ),
    "EXPANSION": (
        "J",
        "PICO GPIO EXPANSION",
        [
            pin(1, "GP22", "passive", "left"),
            pin(2, "GP26_ADC0", "passive", "left"),
            pin(3, "3V3_OUT", "passive", "left"),
            pin(4, "GND", "passive", "left"),
        ],
    ),
    "SSD1315_OLED": (
        "A",
        "SSD1315 128X32 OLED",
        [
            # Physical header order on the module: GND, VCC, SCK, SDA.
            pin("GND", "GND", "power_in", "left"),
            pin("VCC", "VCC", "power_in", "left"),
            pin("SCK", "SCK", "input", "left"),
            pin("SDA", "SDA", "bidirectional", "left"),
        ],
    ),
    "PWR_FLAG": (
        "#FLG", "PWR_FLAG", [pin(1, "PWR_FLAG", "power_out", "bottom")]
    ),
}


def build_symbol_geometry(symbol_name: str, *, embedded: bool) -> SymbolGeometry:
    reference_prefix, default_value, pin_defs = SYMBOL_SPECS[symbol_name]
    grouped = {side: [item for item in pin_defs if item.side == side] for side in ("left", "right", "top", "bottom")}
    max_vertical = max(len(grouped["left"]), len(grouped["right"]), 2)
    max_horizontal = max(len(grouped["top"]), len(grouped["bottom"]), 2)
    half_height = max(7.62, ((max_vertical - 1) * GRID) / 2 + GRID)
    half_width = max(12.7, ((max_horizontal - 1) * GRID) / 2 + GRID)
    pin_positions: dict[str, tuple[float, float, str]] = {}
    symbol_pins: list[SymbolPin] = []

    for side in ("left", "right"):
        items = grouped[side]
        start_y = -(len(items) // 2) * GRID
        for index, item in enumerate(items):
            x = -half_width - GRID if side == "left" else half_width + GRID
            y = start_y + index * GRID
            angle = 0 if side == "left" else 180
            pin_positions[item.number] = (x, y, side)
            symbol_pins.append(SymbolPin(
                electricalType=item.electrical_type,
                position=pos(x, y, angle),
                length=GRID,
                name=item.name,
                number=item.number,
                nameEffects=effects(1.0),
                numberEffects=effects(1.0),
            ))

    for side in ("top", "bottom"):
        items = grouped[side]
        start_x = -(len(items) // 2) * GRID
        for index, item in enumerate(items):
            x = start_x + index * GRID
            y = -half_height - GRID if side == "top" else half_height + GRID
            angle = 270 if side == "top" else 90
            pin_positions[item.number] = (x, y, side)
            symbol_pins.append(SymbolPin(
                electricalType=item.electrical_type,
                position=pos(x, y, angle),
                length=GRID,
                name=item.name,
                number=item.number,
                nameEffects=effects(1.0),
                numberEffects=effects(1.0),
            ))

    unit = Symbol(
        entryName=symbol_name,
        unitId=1,
        styleId=1,
        graphicItems=[SyRect(
            start=pos(-half_width, -half_height),
            end=pos(half_width, half_height),
            stroke=Stroke(width=0.254),
            fill=Fill(type="background"),
        )],
        pins=symbol_pins,
    )
    symbol = Symbol(
        libraryNickname="Z80SBC" if embedded else None,
        entryName=symbol_name,
        pinNames=True,
        pinNamesOffset=1.0,
        inBom=symbol_name != "PWR_FLAG",
        onBoard=symbol_name != "PWR_FLAG",
        properties=[
            property_value("Reference", reference_prefix, 0, -half_height - 5.08),
            property_value("Value", default_value, 0, half_height + 5.08),
            property_value("Footprint", "", hide=True),
            property_value("Datasheet", "", hide=True),
            property_value("Description", f"Z80ROMlessSBC project symbol: {default_value}", hide=True),
            property_value("LCSC", "", hide=True),
        ],
        units=[unit],
    )
    return SymbolGeometry(symbol, pin_positions, half_width, half_height)


embedded_geometry = {name: build_symbol_geometry(name, embedded=True) for name in SYMBOL_SPECS}
library_geometry = {name: build_symbol_geometry(name, embedded=False) for name in SYMBOL_SPECS}


ROOT_UUID = uid("schematic-root")
schematic = Schematic(
    version="20250114",
    generator="eeschema",
    uuid=ROOT_UUID,
    paper=PageSettings(paperSize="A0"),
    titleBlock=TitleBlock(
        title="Z80ROMlessSBC - Partial-SMT JLCPCB Assembly Variant",
        date="2026-11-05",
        revision="SMT-A",
        company="Z80ROMlessSBC",
        comments={1: "Partial-SMT variant of the Rev B schematic; JLC-assembled SMT parts carry LCSC numbers"},
    ),
    libSymbols=[geometry.symbol for geometry in embedded_geometry.values()],
    sheetInstances=[HierarchicalSheetInstance(instancePath="/", page="1")],
)

placements: dict[str, tuple[str, float, float]] = {}
pin_points: dict[tuple[str, str], tuple[float, float, str]] = {}
net_endpoints: dict[str, list[str]] = {}
component_details: dict[str, dict[str, str]] = {}
references: set[str] = set()


def instance_properties(
    reference: str,
    value: str,
    description: str,
    x: float,
    y: float,
    geometry: SymbolGeometry,
    footprint: str,
    datasheet: str = "",
    lcsc: str = "",
) -> list[Property]:
    return [
        property_value("Reference", reference, x, y - geometry.half_height - 5.08),
        property_value("Value", value, x, y + geometry.half_height + 5.08),
        property_value("Footprint", footprint, x, y, hide=True),
        property_value("Datasheet", datasheet, x, y, hide=True),
        property_value("Description", description, x, y, hide=True),
        property_value("LCSC", lcsc, x, y, hide=True),
    ]


def add_component(
    reference: str,
    symbol_name: str,
    value: str,
    x: float,
    y: float,
    pin_nets: dict[int | str, str | None],
    *,
    footprint: str = "",
    description: str | None = None,
    datasheet: str = "",
    lcsc: str = "",
    assembly: str = "",
) -> None:
    if reference in references:
        raise ValueError(f"duplicate reference {reference}")
    references.add(reference)
    geometry = embedded_geometry[symbol_name]
    expected_pins = set(geometry.pin_positions)
    supplied_pins = {str(number) for number in pin_nets}
    if expected_pins != supplied_pins:
        missing = sorted(expected_pins - supplied_pins)
        extra = sorted(supplied_pins - expected_pins)
        raise ValueError(f"{reference} pin assignment mismatch: missing={missing}, extra={extra}")

    symbol_uuid = uid(f"symbol:{reference}")
    schematic.schematicSymbols.append(SchematicSymbol(
        libraryNickname="Z80SBC",
        entryName=symbol_name,
        position=pos(x, y, 0),
        unit=1,
        inBom=symbol_name != "PWR_FLAG",
        onBoard=symbol_name != "PWR_FLAG",
        uuid=symbol_uuid,
        properties=instance_properties(
            reference, value,
            description or f"Z80ROMlessSBC {value}",
            x, y, geometry, footprint, datasheet, lcsc,
        ),
        pins={
            number: uid(f"pin:{reference}:{number}")
            for number in sorted(expected_pins, key=lambda pin: (len(pin), pin))
        },
        instances=[SymbolProjectInstance(
            name=PROJECT_NAME,
            paths=[SymbolProjectPath(
                sheetInstancePath=f"/{ROOT_UUID}",
                reference=reference,
                unit=1,
            )],
        )],
    ))
    placements[reference] = (symbol_name, x, y)
    component_details[reference] = {
        "symbol": symbol_name,
        "value": value,
        "footprint": footprint,
        "description": description or f"Z80ROMlessSBC {value}",
        "lcsc": lcsc,
        "assembly": assembly,
    }
    if symbol_name != "PWR_FLAG" and assembly not in {"jlc", "hand"}:
        raise ValueError(f"{reference} must declare JLC or hand assembly")
    if assembly == "jlc" and not lcsc:
        raise ValueError(f"{reference} is JLC-assembled but has no LCSC part")

    for raw_number, net in pin_nets.items():
        number = str(raw_number)
        local_x, local_y, side = geometry.pin_positions[number]
        point = (round(x + local_x, 4), round(y - local_y, 4), side)
        pin_points[(reference, number)] = point
        if net is None:
            schematic.noConnects.append(NoConnect(
                position=pos(point[0], point[1]),
                uuid=uid(f"nc:{reference}:{number}"),
            ))
            continue
        manifest_net = "RESET_N" if net == "@RESET_JUNCTION" else net
        net_endpoints.setdefault(manifest_net, []).append(f"{reference}.{number}")
        if net == "@RESET_JUNCTION":
            continue
        add_labeled_stub(reference, number, net, point)


def add_labeled_stub(
    reference: str,
    number: str,
    net: str,
    point: tuple[float, float, str],
) -> None:
    x, y, side = point
    offsets = {
        "left": (-GRID, 0, 180),
        "right": (GRID, 0, 0),
        "top": (0, -GRID, 90),
        "bottom": (0, GRID, 270),
    }
    dx, dy, angle = offsets[side]
    label_x, label_y = round(x + dx, 4), round(y + dy, 4)
    schematic.graphicalItems.append(Connection(
        type="wire",
        points=[pos(x, y), pos(label_x, label_y)],
        stroke=Stroke(width=0),
        uuid=uid(f"wire:{reference}:{number}:{net}"),
    ))
    schematic.labels.append(LocalLabel(
        text=net,
        position=pos(label_x, label_y, angle),
        effects=effects(1.0),
        uuid=uid(f"label:{reference}:{number}:{net}"),
    ))


def pin_map(entries: list[tuple[int | str, str | None]]) -> dict[int | str, str | None]:
    return dict(entries)


z80_nets = {
    1: "A11", 2: "A12", 3: "A13", 4: "A14", 5: "A15", 6: "Z80_CLK",
    7: "D4", 8: "D3", 9: "D5", 10: "D6", 11: "+5V", 12: "D2", 13: "D7",
    14: "D0", 15: "D1", 16: "INT_N", 17: "NMI_N", 18: None, 19: "MREQ_N",
    20: "IORQ_N", 21: "RD_N", 22: "WR_N", 23: "BUSACK_N", 24: "WAIT_N",
    25: "BUSREQ_N", 26: "@RESET_JUNCTION", 27: None, 28: None, 29: "GND",
    30: "A0", 31: "A1", 32: "A2", 33: "A3", 34: "A4", 35: "A5", 36: "A6",
    37: "A7", 38: "A8", 39: "A9", 40: "A10",
}
add_component(
    "U1", "Z80_CPU", "Z84C0020PEC", 508.0, 152.4, z80_nets,
    footprint="Package_DIP:DIP-40_W15.24mm",
    description="Z80 CPU, DIP-40 soldered directly to the board (hand assembly)",
    assembly="hand",
)

# IS62C1024AL SOP-32 uses the AS6C1008 DIP pin assignment.
sram_nets = {
    1: None, 2: "SRAM_A16", 3: "A14", 4: "A12", 5: "A7", 6: "A6", 7: "A5",
    8: "A4", 9: "A3", 10: "A2", 11: "A1", 12: "A0", 13: "D0", 14: "D1",
    15: "D2", 16: "GND", 17: "D3", 18: "D4", 19: "D5", 20: "D6", 21: "D7",
    22: "SRAM_CE_N", 23: "A10", 24: "SRAM_OE_N", 25: "A11", 26: "A9",
    27: "A8", 28: "A13", 29: "SRAM_WE_N", 30: "+5V", 31: "A15", 32: "+5V",
}
add_component(
    "U2", "SRAM", "IS62C1024AL-35QLI", 152.4, 152.4, sram_nets,
    footprint="Package_SO:SOP-32_11.305x20.495mm_P1.27mm",
    description="128K x 8 5V TTL-level SRAM, 35 ns, SOP-32 450 mil",
    datasheet="https://www.issi.com/WW/pdf/62C1024AL.pdf",
    lcsc="C397875", assembly="jlc",
)

hct32_nets = {
    1: "RD_5V_N", 2: "BOOT_READ_DISABLE_5V", 3: "SRAM_OE_N",
    4: "IORQ_N", 5: "IO_RELEASE", 6: "WAIT_N", 7: "GND",
    8: "DATA_UP_OE_N", 9: "RD_N", 10: "PICO_DATA_UP_OE_N",
    11: None, 12: "GND", 13: "GND", 14: "+5V",
}
add_component(
    "U3", "HCT32", "SN74HCT32DR", 152.4, 330.2, hct32_nets,
    footprint="Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
    lcsc="C6781", assembly="jlc",
)

ahct244_nets = {
    1: "GND", 2: "PICO_CLK", 3: "SRAM_A16", 4: "PICO_BUSREQ_N",
    5: None, 6: "BOOT_READ_DISABLE", 7: "RD_5V_N",
    8: "MREQ_N", 9: "SRAM_WE_N", 10: "GND", 11: "WR_N",
    12: "SRAM_CE_N", 13: "RD_N", 14: "BOOT_READ_DISABLE_5V",
    15: "GND", 16: "BUSREQ_N", 17: "PICO_GP8",
    18: "Z80_CLK", 19: "GND", 20: "+5V",
}
add_component(
    "U4", "AHCT244", "SN74AHCT244DWR", 508.0, 330.2, ahct244_nets,
    footprint="Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm",
    lcsc="C2675568", assembly="jlc",
)

lvc244_nets = {
    1: "GND", 2: "BUSACK_N", 3: "PICO_A4", 4: "IORQ_N", 5: "PICO_A2", 6: "RD_N",
    7: "PICO_A1", 8: "WR_N", 9: "PICO_A0", 10: "GND", 11: "A0",
    12: "PICO_WR_N", 13: "A1", 14: "PICO_RD_N", 15: "A2",
    16: "PICO_IORQ_N", 17: "A4", 18: "PICO_BUSACK_N", 19: "GND", 20: "+3V3",
}
add_component(
    "U7", "LVC244", "SN74LVC244ADWR", 812.8, 482.6, lvc244_nets,
    footprint="Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm",
    lcsc="C507250", assembly="jlc",
)

up_nets = {1: "+5V", 10: "GND", 19: "DATA_UP_OE_N", 20: "+5V"}
down_nets = {1: "GND", 10: "GND", 19: "DATA_DOWN_OE_N", 20: "+3V3"}
for bit in range(8):
    up_nets[2 + bit] = f"PICO_D{bit}"
    up_nets[18 - bit] = f"D{bit}"
    down_nets[2 + bit] = f"PICO_D{bit}"
    down_nets[18 - bit] = f"D{bit}"
add_component(
    "U9", "AHCT245", "SN74AHCT245DWR", 812.8, 304.8, up_nets,
    footprint="Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm",
    description="Pico-to-bus data buffer",
    lcsc="C94773", assembly="jlc",
)
add_component(
    "U10", "LVC245", "SN74LVC245ADWR", 914.4, 304.8, down_nets,
    footprint="Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm",
    description="Bus-to-Pico data buffer",
    lcsc="C571201", assembly="jlc",
)

pico_nets = {
    1: "PICO_BUSACK_N", 2: "PICO_IORQ_N", 3: "GND", 4: "PICO_CLK",
    5: "@RESET_JUNCTION", 6: "PICO_BUSREQ_N", 7: "BOOT_READ_DISABLE", 8: "GND",
    9: "PICO_DATA_UP_OE_N", 10: "DATA_DOWN_OE_N", 11: "PICO_GP8", 12: "IO_RELEASE", 13: "GND",
    14: "PICO_D0", 15: "PICO_D1", 16: "PICO_D2", 17: "PICO_D3", 18: "GND",
    19: "PICO_D4", 20: "PICO_D5", 21: "PICO_D6", 22: "PICO_D7", 23: "GND",
    24: "PICO_A0", 25: "PICO_A1", 26: "PICO_A2",
    27: "PICO_A4", 28: "GND", 29: "PICO_GP22", 30: None,
    31: "PICO_GP26", 32: "PICO_RD_N", 33: "GND", 34: "PICO_WR_N",
    35: None, 36: "+3V3", 37: None, 38: "GND", 39: "VSYS", 40: None,
}
add_component(
    "A1", "PICO2", "Raspberry Pi Pico 2 W", 863.6, 152.4, pico_nets,
    footprint="Module:RaspberryPi_Pico_Common_THT",
    description="Raspberry Pi Pico 2 W, through-hole edge pads (hand assembly)",
    assembly="hand",
)

add_component(
    "J1", "USB_C_POWER", "USB-C 5V power", 50.8, 50.8,
    {
        "A5": "USB_CC1", "B5": "USB_CC2",
        "A6": None, "A7": None, "B6": None, "B7": None, "A8": None, "B8": None,
        "A4": "+5V", "A9": "+5V", "B4": "+5V", "B9": "+5V",
        "A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "SH": "GND",
    },
    footprint="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
    description="SHOU HAN TYPE-C 16PIN power-only receptacle; VBUS feeds +5V directly; CC1/CC2 each 5.1k to GND; USB data and SBU unconnected; shell to GND",
    datasheet="https://jlcpcb.com/partdetail/TYPE-C16PIN/C393939",
    lcsc="C393939", assembly="jlc",
)
add_component(
    "R41", "RESISTOR", "5.1k", 330.2, 30.48, {1: "USB_CC1", 2: "GND"},
    footprint="Resistor_SMD:R_0603_1608Metric",
    description="USB-C CC1 sink advertisement, 5.1k 1% 0603",
    lcsc="C23186", assembly="jlc",
)
add_component(
    "R42", "RESISTOR", "5.1k", 330.2, 55.88, {1: "USB_CC2", 2: "GND"},
    footprint="Resistor_SMD:R_0603_1608Metric",
    description="USB-C CC2 sink advertisement, 5.1k 1% 0603",
    lcsc="C23186", assembly="jlc",
)
add_component(
    "J2", "EXPANSION", "PICO GPIO EXPANSION", 1016.0, 152.4,
    {1: "PICO_GP22", 2: "PICO_GP26", 3: "+3V3", 4: "GND"},
    footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    description="3.3V header: GP22/SDA, GP26/SCL/ADC0, Pico 3V3 output, GND; GP8 is dedicated to SRAM bank select and not exposed",
    assembly="hand",
)
add_component(
    "J3", "POWER_HEADER", "EXTERNAL 5V", 50.8, 101.6,
    {1: "+5V", 2: "GND"},
    footprint="Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
    description="Optional auxiliary 5V input/probe header: pin 1 +5V, pin 2 GND; shares J1 rail without source isolation, use only one supply at a time; never connect to Pico VBUS",
    assembly="hand",
)
add_component(
    "A2", "SSD1315_OLED", "0.91in SSD1315 128X32 I2C OLED", 1016.0, 254.0,
    {"VCC": "+3V3", "GND": "GND", "SCK": "PICO_GP26", "SDA": "PICO_GP22"},
    footprint="Z80SBC:OLED_SSD1315_38x12mm_Socket",
    description="Socketed Stage 10 status OLED, 38.00 x 12.00 mm body; header centres 1.50 mm from left edge, 2.19 mm from top for SDA, 2.54 mm pitch; GND, VCC, SCK, SDA bottom-to-top; I2C 0x3C, pull-ups to 3.3V; module hovers over low-profile SMT parts",
    datasheet="https://www.solomon-systech.com/product/ssd1315",
    assembly="hand",
)
add_component(
    "D1", "DIODE", "SS14", 127.0, 50.8, {1: "VSYS", 2: "+5V"},
    footprint="Diode_SMD:D_SMA",
    description="1A 40V Schottky, +5V to Pico VSYS; cathode toward VSYS",
    lcsc="C2480", assembly="jlc",
)
add_component("#FLG01", "PWR_FLAG", "PWR_FLAG", 203.2, 50.8, {1: "VSYS"})
add_component("#FLG02", "PWR_FLAG", "PWR_FLAG", 254.0, 50.8, {1: "+5V"})
add_component("#FLG03", "PWR_FLAG", "PWR_FLAG", 254.0, 25.4, {1: "GND"})


# Discrete 0603 pull resistors replace the Rev B bussed networks RN1-RN6.
PULL_RESISTORS = (
    [(f"A{bit}", "+5V", "Z80 address pull-up") for bit in range(16)]
    + [(f"PICO_D{bit}", "GND", "Pico data pull-down") for bit in range(8)]
    + [(net, "+5V", "Z80 control pull-up") for net in (
        "BUSREQ_N", "BUSACK_N", "MREQ_N", "IORQ_N",
        "RD_N", "WR_N", "INT_N", "NMI_N",
    )]
    + [(net, "+3V3", "startup pull-up") for net in (
        "PICO_BUSREQ_N", "BOOT_READ_DISABLE",
        "PICO_DATA_UP_OE_N", "DATA_DOWN_OE_N",
    )]
    + [(net, "GND", "startup pull-down") for net in (
        "PICO_CLK", "RESET_N", "IO_RELEASE", "PICO_GP8",
    )]
)
if len(PULL_RESISTORS) != 40:
    raise AssertionError("pull resistor count must remain 40")
for index, (net, rail, role) in enumerate(PULL_RESISTORS, start=1):
    column, row = (index - 1) % 10, (index - 1) // 10
    add_component(
        f"R{index}", "RESISTOR", "10k",
        50.8 + column * 55.88, 548.64 + row * 38.1,
        {1: net, 2: rail},
        footprint="Resistor_SMD:R_0603_1608Metric",
        description=f"{net} {role} to {rail}, 10k 1% 0603",
        lcsc="C25804", assembly="jlc",
    )


CAPACITORS = [
    ("100n", "U1 Z80 bypass", "+5V"),
    ("100n", "U2 SRAM bypass", "+5V"),
    ("100n", "U3 HCT32 bypass", "+5V"),
    ("100n", "U4 AHCT244 bypass", "+5V"),
    ("100n", "U7 LVC244 bypass", "+3V3"),
    ("100n", "U9 AHCT245 bypass", "+5V"),
    ("100n", "U10 LVC245 bypass", "+3V3"),
    ("22u", "Memory cluster bulk", "+5V"),
    ("22u", "Core cluster bulk", "+5V"),
    ("22u", "Peripheral cluster bulk", "+5V"),
    ("100u", "Supply-entry bulk", "+5V"),
]
CAPACITOR_PARTS = {
    "100n": ("Capacitor_SMD:C_0603_1608Metric", "C14663", "100nF 50V X7R 0603"),
    "22u": ("Capacitor_SMD:CP_Elec_6.3x5.4", "C150379", "22uF 35V SMT electrolytic, 6.3 x 5 mm"),
    "100u": ("Capacitor_SMD:CP_Elec_6.3x5.4", "C346929", "100uF 16V SMT electrolytic, 6.3 x 5 mm"),
}
if sum(value == "100n" for value, _, _ in CAPACITORS) != 7:
    raise AssertionError("decoupling count must remain 7")
for index, (value, role, rail) in enumerate(CAPACITORS, start=1):
    footprint, lcsc, specification = CAPACITOR_PARTS[value]
    add_component(
        f"C{index}", "CAPACITOR", value,
        50.8 + (index - 1) * 76.2, 787.4,
        {1: rail, 2: "GND"},
        footprint=footprint,
        description=f"{role}: {specification}",
        lcsc=lcsc, assembly="jlc",
    )



BUS_ROUTES = [
    ("ADDRESS", [f"A{bit}" for bit in range(16)], [(220.98, 208.28), (439.42, 208.28)]),
    ("DATA", [f"D{bit}" for bit in range(8)], [(220.98, 228.6), (439.42, 228.6)]),
    ("CPU_TO_BUFFERS", ["BUSACK_N", "MREQ_N", "RD_N", "WR_N"], [(220.98, 299.72), (439.42, 299.72)]),
    ("BUFFERED_CONTROLS", ["SRAM_WE_N", "RD_5V_N", "SRAM_CE_N"], [(220.98, 327.66), (439.42, 327.66)]),
    ("HCT_TO_SRAM", ["SRAM_WE_N", "SRAM_OE_N", "SRAM_CE_N"], [(220.98, 355.6), (439.42, 355.6)]),
    ("BOOT_CONTROL", ["BOOT_READ_DISABLE", "BOOT_READ_DISABLE_5V", "IO_RELEASE"], [(220.98, 109.22), (779.78, 109.22)]),
    ("HCT_TO_CPU", ["Z80_CLK", "BUSREQ_N"], [(457.2, 261.62), (558.8, 261.62)]),
    ("PICO_DATA", [f"PICO_D{bit}" for bit in range(8)], [(617.22, 208.28), (779.78, 208.28)]),
    ("PICO_TO_BUFFER", ["PICO_CLK", "PICO_BUSREQ_N", "BOOT_READ_DISABLE"], [(617.22, 279.4), (779.78, 279.4)]),
    ("CPU_MONITOR", ["BUSACK_N", "IORQ_N", "RD_N", "WR_N"], [(617.22, 375.92), (779.78, 375.92)]),
    ("PICO_MONITOR", ["PICO_BUSACK_N", "PICO_IORQ_N", "PICO_RD_N", "PICO_WR_N"], [(617.22, 403.86), (779.78, 403.86)]),
    ("PORT_MONITOR", ["PICO_A0", "PICO_A1", "PICO_A2", "PICO_A4"], [(220.98, 431.8), (779.78, 431.8)]),
    ("DATA_CONTROL", ["PICO_DATA_UP_OE_N", "DATA_UP_OE_N", "DATA_DOWN_OE_N"], [(220.98, 457.2), (779.78, 457.2)]),
]
VECTOR_BUS_LABELS = {
    "ADDRESS": "A[0..15]",
    "DATA": "D[0..7]",
    "PICO_DATA": "PICO_D[0..7]",
}
for alias_name, members, points in BUS_ROUTES:
    for segment_index, (start, end) in enumerate(zip(points, points[1:]), start=1):
        schematic.graphicalItems.append(Connection(
            type="bus",
            points=[pos(*start), pos(*end)],
            stroke=Stroke(width=0),
            uuid=uid(f"bus:{alias_name}:{segment_index}"),
        ))
    schematic.labels.append(LocalLabel(
        text=VECTOR_BUS_LABELS.get(alias_name, "{" + " ".join(members) + "}"),
        position=pos(points[0][0], points[0][1], 0),
        effects=effects(1.27),
        uuid=uid(f"bus-label:{alias_name}"),
    ))
    schematic.texts.append(Text(
        text=alias_name,
        position=pos((points[0][0] + points[-1][0]) / 2, points[0][1] + 5.08, 0),
        effects=Effects(font=Font(height=1.27, width=1.27, bold=True)),
        uuid=uid(f"bus-title:{alias_name}"),
    ))
    route_start_x, route_y = points[0]
    route_end_x = points[-1][0]
    available_width = route_end_x - route_start_x - 10.16
    spacing = 0 if len(members) == 1 else min(17.78, available_width / (len(members) - 1))
    spacing = max(GRID, round(spacing / 1.27) * 1.27)
    for member_index, member in enumerate(members):
        entry_x = route_start_x + 5.08 + member_index * spacing
        entry_end_x = entry_x + GRID
        entry_end_y = route_y - GRID
        label_y = route_y - 7.62
        schematic.busEntries.append(BusEntry(
            position=pos(entry_x, route_y),
            size=pos(GRID, -GRID),
            stroke=Stroke(width=0),
            uuid=uid(f"bus-entry:{alias_name}:{member}"),
        ))
        schematic.graphicalItems.append(Connection(
            type="wire",
            points=[pos(entry_end_x, entry_end_y), pos(entry_end_x, label_y)],
            stroke=Stroke(width=0),
            uuid=uid(f"bus-member-wire:{alias_name}:{member}"),
        ))
        schematic.labels.append(LocalLabel(
            text=member,
            position=pos(entry_end_x, label_y, 90),
            effects=effects(0.9),
            uuid=uid(f"bus-member-label:{alias_name}:{member}"),
        ))


reset_points = [pin_points[("U1", "26")], pin_points[("A1", "5")]]
reset_bus_y = 76.2
reset_xs = [point[0] for point in reset_points]
schematic.graphicalItems.append(Connection(
    type="wire",
    points=[pos(min(reset_xs), reset_bus_y), pos(max(reset_xs), reset_bus_y)],
    stroke=Stroke(width=0),
    uuid=uid("wire:reset:bus"),
))
for index, point in enumerate(reset_points, start=1):
    x, y, _ = point
    schematic.graphicalItems.append(Connection(
        type="wire",
        points=[pos(x, y), pos(x, reset_bus_y)],
        stroke=Stroke(width=0),
        uuid=uid(f"wire:reset:branch:{index}"),
    ))
    schematic.junctions.append(Junction(
        position=pos(x, reset_bus_y),
        uuid=uid(f"junction:reset:{index}"),
    ))
schematic.labels.append(LocalLabel(
    text="RESET_N",
    position=pos(reset_points[1][0], reset_bus_y, 0),
    effects=effects(1.27),
    uuid=uid("label:reset:bus"),
))


for text, x in (("MEMORY", 152.4), ("CORE", 508.0), ("PERIPHERAL", 863.6)):
    schematic.texts.append(Text(
        text=text,
        position=pos(x, 91.44, 0),
        effects=Effects(font=Font(height=2.54, width=2.54, bold=True)),
        uuid=uid(f"text:{text}"),
    ))
schematic.texts.extend([
    Text(
        text="J1: power-only USB-C receptacle (JLC C393939).\\nVBUS -> +5V directly; no regulator, eFuse or OVP.\\nCC1 and CC2 each 5.1k to GND (R41, R42).\\nD+/D-/SBU unconnected; shell to GND.\\nUse a regulated 5V USB supply only.\\nJ3 is auxiliary: only ONE supply at a time.\\nKeep D1 to Pico VSYS; never join Pico VBUS.",
        position=pos(381.0, 25.4, 0),
        effects=effects(1.27),
        uuid=uid("text:usb-c-power-note"),
    ),
    Text(
        text="Z80 is the sole SRAM address/write master. Pico injects instructions with SRAM reads inhibited. RESET_N directly joins Pico and Z80 at 3.3V.",
        position=pos(508.0, 38.1, 0),
        effects=effects(1.27),
        uuid=uid("text:reset-note"),
    ),
    Text(
        text="R1-R40: discrete 10k 0603 pulls replacing the Rev B networks. R1-R16 A0-A15 to +5V; R17-R24 PICO_D0-D7 to GND; R25-R32 Z80 controls to +5V; R33-R36 3V3 startup pull-ups (breadboard R17-R20); R37-R40 startup pull-downs (breadboard R21-R24). Both data paths default disabled.",
        position=pos(50.8, 520.7, 0),
        effects=effects(1.27),
        uuid=uid("text:pull-note"),
    ),
    Text(
        text="J2: 3.3V expansion/probe header, not a 5V input.\\n1: GP22/SDA; 2: GP26/SCL, shared with A2.\\n3: Pico 3V3 output; 4: GND.\\nGP8 is dedicated to SRAM bank select.",
        position=pos(1016.0, 190.5, 0),
        effects=effects(1.27),
        uuid=uid("text:expansion-note"),
    ),
    Text(
        text="BANK SELECT: Pico physical pin 11 / GP8 -> U4.17.\\nR40: 10k to GND (breadboard R24 equivalent).\\nU4.3 -> U2.2 / A16 (5V side); LOW selects bank 0.",
        position=pos(787.4, 254.0, 0),
        effects=effects(1.27),
        uuid=uid("text:bank-select-note"),
    ),
    Text(
        text="A2: socketed Stage 10 0.91in SSD1315 128x32 status OLED.\\nModule 38.00 x 12.00 mm; 4-pin 2.54 mm header.\\nSocket: 1x4 2.54 mm female header, 5.0 mm body (hand fit).\\nHeader order GND, VCC, SCK, SDA bottom-to-top.\\nSDA = J2.1 / GP22; SCK = J2.2 / GP26.\\nVCC = J2.3 / 3V3; GND = J2.4 / GND.\\nI2C pull-ups to 3.3V only; address 0x3C.\\nModule hovers over low-profile SMT parts only.",
        position=pos(1016.0, 292.1, 0),
        effects=effects(1.27),
        uuid=uid("text:oled-note"),
    ),
])


required_endpoints = {
    "+5V": {"J1.A4", "J1.A9", "J1.B4", "J1.B9", "J3.1", "D1.2"},
    "VSYS": {"D1.1", "A1.39"},
    "GND": {"J1.A1", "J1.A12", "J1.B1", "J1.B12", "J1.SH", "A1.38", "J2.4", "A2.GND", "R41.2", "R42.2"},
    "USB_CC1": {"J1.A5", "R41.1"},
    "USB_CC2": {"J1.B5", "R42.1"},
    "+3V3": {"A1.36", "J2.3", "A2.VCC"},
    "A0": {"U1.30", "U2.12", "U7.11", "R1.1"},
    "A8": {"U1.38", "U2.27", "R9.1"},
    "D0": {"U1.14", "U2.13", "U9.18", "U10.18"},
    "PICO_D0": {"A1.14", "U9.2", "U10.2", "R17.1"},
    "PICO_DATA_UP_OE_N": {"A1.9", "R35.1", "U3.10"},
    "DATA_UP_OE_N": {"U3.8", "U9.19"},
    "DATA_DOWN_OE_N": {"A1.10", "R36.1", "U10.19"},
    "BOOT_READ_DISABLE": {"A1.7", "R34.1", "U4.6"},
    "IO_RELEASE": {"A1.12", "R39.1", "U3.5"},
    "RESET_N": {"U1.26", "A1.5", "R38.1"},
}
for net, required in required_endpoints.items():
    actual = set(net_endpoints.get(net, []))
    if not required <= actual:
        raise AssertionError(f"{net} missing endpoints: {sorted(required - actual)}")

for net, required in {
    "PICO_GP8": {"A1.11", "U4.17", "R40.1"},
    "SRAM_A16": {"U4.3", "U2.2"},
    "PICO_GP22": {"A1.29", "J2.1", "A2.SDA"},
    "PICO_GP26": {"A1.31", "J2.2", "A2.SCK"},
    "USB_CC1": {"J1.A5", "R41.1"},
    "USB_CC2": {"J1.B5", "R42.1"},
}.items():
    actual = set(net_endpoints.get(net, []))
    if actual != required:
        raise AssertionError(f"{net} endpoints differ: expected {required}, got {actual}")

physical_references = sorted(reference for reference in references if not reference.startswith("#"))
erc_references = sorted(reference for reference in references if reference.startswith("#"))
manifest = {
    "generated_by": "scripts/build-kicad-smt-schematic.py",
    "schematic": str(SCHEMATIC_PATH.relative_to(REPO_ROOT)),
    "component_count": len(physical_references),
    "components": physical_references,
    "component_details": {
        reference: component_details[reference]
        for reference in physical_references
    },
    "erc_symbols": erc_references,
    "breadboard_resistor_equivalents": {
        f"R{index + start}": {"1": f"R{index + first}.1", "2": f"R{index + first}.2"}
        for first, start in ((33, 17), (37, 21))
        for index in range(4)
    },
    "nets": {net: sorted(endpoints) for net, endpoints in sorted(net_endpoints.items())},
}
MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

SYMBOL_PATH.parent.mkdir(parents=True, exist_ok=True)
SymbolLib(
    version="20231120",
    generator="kicad_symbol_editor",
    symbols=[geometry.symbol for geometry in library_geometry.values()],
).to_file(str(SYMBOL_PATH))
schematic.to_file(str(SCHEMATIC_PATH))
print(f"Wrote {SCHEMATIC_PATH}")
print(f"Wrote {SYMBOL_PATH}")
print(f"Wrote {MANIFEST_PATH}")
