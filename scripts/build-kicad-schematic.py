#!/usr/bin/env python3
"""Generate the native KiCad schematic and exact machine-readable net manifest."""

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
KICAD_DIR = REPO_ROOT / "hardware" / "kicad"
SCHEMATIC_PATH = KICAD_DIR / "z80_romless_sbc.kicad_sch"
SYMBOL_PATH = KICAD_DIR / "z80sbc.kicad_sym"
MANIFEST_PATH = KICAD_DIR / "reports" / "net_manifest.json"
PROJECT_NAME = "z80_romless_sbc"
UUID_NAMESPACE = UUID("da1bd5db-a020-44a8-b5e5-4ce976084a9d")
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
        "AS6C1008-55PCN",
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
    "ATF22V10": (
        "U",
        "ATF22V10B/C",
        [
            pin(1, "RESET#", "input", "left"), pin(2, "BUSACK#", "input", "left"),
            pin(3, "PICO_WE#", "input", "left"), pin(4, "Z80_WR#", "input", "left"),
            pin(5, "PICO_OE#", "input", "left"), pin(6, "Z80_RD#", "input", "left"),
            pin(7, "PICO_CE#", "input", "left"), pin(8, "Z80_MREQ#", "input", "left"),
            pin(9, "DATA_ENABLE", "input", "left"), pin(10, "ADDR_ENABLE", "input", "left"),
            pin(11, "DATA_DIR", "input", "left"), pin(12, "GND", "power_in", "bottom"),
            pin(13, "Z80_IORQ#", "input", "left"), pin(14, "SRAM_WE_PRE#", "output", "right"),
            pin(15, "SRAM_OE_PRE#", "output", "right"), pin(16, "SRAM_CE_PRE#", "output", "right"),
            pin(17, "DATA_UP_OE#", "output", "right"), pin(18, "DATA_DOWN_OE#", "output", "right"),
            pin(19, "MCP_RESET_DRIVE", "output", "right"), pin(20, "WAIT#", "output", "right"),
            *[pin(number, f"CONST_LOW{number - 20}", "output", "right") for number in range(21, 24)],
            pin(24, "VCC", "power_in", "top"),
        ],
    ),
    "AHCT244": (
        "U",
        "SN74AHCT244N",
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
        "SN74AHCT245N",
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
        "SN74LVC245AN",
        [
            pin(1, "DIR", "input", "left"),
            *[pin(number + 1, f"A{number}", "bidirectional", "left") for number in range(1, 9)],
            pin(10, "GND", "power_in", "bottom"),
            *[pin(19 - number, f"B{number}", "bidirectional", "right") for number in range(1, 9)],
            pin(19, "OE#", "input", "left"), pin(20, "VCC", "power_in", "top"),
        ],
    ),
    "NPN": (
        "Q",
        "2N3904",
        [
            pin(1, "E", "passive", "bottom"), pin(2, "B", "input", "left"),
            pin(3, "C", "open_collector", "top"),
        ],
    ),
    "RN8": (
        "RN",
        "8x10k bussed",
        [
            pin(1, "COMMON", "passive", "top"),
            *[pin(number + 2, f"R{number + 1}", "passive", "right") for number in range(8)],
        ],
    ),
    "LVC244": (
        "U",
        "SN74LVC244AN",
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
    "MCP23S17": (
        "U",
        "MCP23S17-E/SP",
        [
            *[pin(number + 1, f"GPB{number}", "bidirectional", "right") for number in range(8)],
            pin(9, "VDD", "power_in", "top"), pin(10, "VSS", "power_in", "bottom"),
            pin(11, "CS#", "input", "left"), pin(12, "SCK", "input", "left"),
            pin(13, "SI", "input", "left"), pin(14, "SO", "tri_state", "left"),
            pin(15, "A0", "input", "left"), pin(16, "A1", "input", "left"),
            pin(17, "A2", "input", "left"), pin(18, "RESET#", "input", "left"),
            pin(19, "INTB", "open_collector", "left"), pin(20, "INTA", "open_collector", "left"),
            *[pin(number + 21, f"GPA{number}", "bidirectional", "right") for number in range(8)],
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
        "D", "1N5819", [pin(1, "K", "passive", "right"), pin(2, "A", "passive", "left")]
    ),
    "SUPPLY": (
        "J", "5V INPUT", [pin(1, "+5V", "power_out", "right"), pin(2, "GND", "power_out", "right")]
    ),
    "TESTPOINT": (
        "TP", "M1# TEST", [pin(1, "TP", "passive", "left")]
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
        title="Z80ROMlessSBC - Exact Breadboard Electrical Schematic",
        date="2026-08-23",
        revision="1",
        company="Z80ROMlessSBC",
        comments={1: "Native KiCad 10 schematic generated from the verified README pin map"},
    ),
    libSymbols=[geometry.symbol for geometry in embedded_geometry.values()],
    sheetInstances=[HierarchicalSheetInstance(instancePath="/", page="1")],
)

placements: dict[str, tuple[str, float, float]] = {}
pin_points: dict[tuple[str, str], tuple[float, float, str]] = {}
net_endpoints: dict[str, list[str]] = {}
references: set[str] = set()


def instance_properties(
    reference: str,
    value: str,
    x: float,
    y: float,
    geometry: SymbolGeometry,
    footprint: str,
) -> list[Property]:
    return [
        property_value("Reference", reference, x, y - geometry.half_height - 5.08),
        property_value("Value", value, x, y + geometry.half_height + 5.08),
        property_value("Footprint", footprint, x, y, hide=True),
        property_value("Datasheet", "", x, y, hide=True),
        property_value("Description", f"Z80ROMlessSBC {value}", x, y, hide=True),
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
        properties=instance_properties(reference, value, x, y, geometry, footprint),
        pins={number: uid(f"pin:{reference}:{number}") for number in expected_pins},
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
    25: "BUSREQ_N", 26: "@RESET_JUNCTION", 27: "M1_N", 28: None, 29: "GND",
    30: "A0", 31: "A1", 32: "A2", 33: "A3", 34: "A4", 35: "A5", 36: "A6",
    37: "A7", 38: "A8", 39: "A9", 40: "A10",
}
add_component("U1", "Z80_CPU", "Z84C0020PEC", 508.0, 152.4, z80_nets, footprint="Package_DIP:DIP-40_W15.24mm")

sram_nets = {
    1: None, 2: "GND", 3: "A14", 4: "A12", 5: "A7", 6: "A6", 7: "A5",
    8: "A4", 9: "A3", 10: "A2", 11: "A1", 12: "A0", 13: "D0", 14: "D1",
    15: "D2", 16: "GND", 17: "D3", 18: "D4", 19: "D5", 20: "D6", 21: "D7",
    22: "SRAM_CE_N", 23: "A10", 24: "SRAM_OE_N", 25: "A11", 26: "A9",
    27: "A8", 28: "A13", 29: "SRAM_WE_N", 30: "+5V", 31: "A15", 32: "+5V",
}
add_component("U2", "SRAM", "AS6C1008-55PCN", 152.4, 152.4, sram_nets, footprint="Package_DIP:DIP-32_W15.24mm")

gal_nets = {
    1: "@RESET_JUNCTION", 2: "BUSACK_N", 3: "PICO_WE_N", 4: "WR_N",
    5: "PICO_OE_N", 6: "RD_N", 7: "PICO_CE_N", 8: "MREQ_N",
    9: "DATA_ENABLE", 10: "ADDR_ENABLE", 11: "DATA_DIR", 12: "GND", 13: "IORQ_N",
    14: "SRAM_WE_PRE_N", 15: "SRAM_OE_PRE_N", 16: "SRAM_CE_PRE_N",
    17: "DATA_UP_OE_N", 18: "DATA_DOWN_OE_N",
    19: "MCP_RESET_DRIVE", 20: "WAIT_N", 21: None, 22: None, 23: None, 24: "+5V",
}
add_component("U3", "ATF22V10", "ATF22V10B/C", 152.4, 330.2, gal_nets, footprint="Package_DIP:DIP-24_W7.62mm")

ahct244_nets = {
    1: "GND", 2: "PICO_CLK", 3: "SRAM_CE_N", 4: "PICO_BUSREQ_N",
    5: "SRAM_OE_N", 6: "PICO_SPI_CS_N", 7: "SRAM_WE_N",
    8: "PICO_SPI_SCK", 9: "MCP_SI", 10: "GND", 11: "PICO_SPI_MOSI",
    12: "MCP_SCK", 13: "SRAM_WE_PRE_N", 14: "MCP_CS_N",
    15: "SRAM_OE_PRE_N", 16: "BUSREQ_N", 17: "SRAM_CE_PRE_N",
    18: "Z80_CLK", 19: "GND", 20: "+5V",
}
add_component("U4", "AHCT244", "SN74AHCT244N", 508.0, 330.2, ahct244_nets, footprint="Package_DIP:DIP-20_W7.62mm")

lvc244_nets = {
    1: "GND", 2: "BUSACK_N", 3: None, 4: "IORQ_N", 5: None, 6: "RD_N",
    7: None, 8: "WR_N", 9: "PICO_SPI_MISO", 10: "GND", 11: "MCP_SO",
    12: "PICO_WR_N", 13: "GND", 14: "PICO_RD_N", 15: "GND",
    16: "PICO_IORQ_N", 17: "GND", 18: "PICO_BUSACK_N", 19: "GND", 20: "+3V3",
}
add_component("U7", "LVC244", "SN74LVC244AN", 812.8, 482.6, lvc244_nets, footprint="Package_DIP:DIP-20_W7.62mm")

mcp_nets = {number + 1: f"A{number + 8}" for number in range(8)}
mcp_nets.update({
    9: "+5V", 10: "GND", 11: "MCP_CS_N", 12: "MCP_SCK", 13: "MCP_SI",
    14: "MCP_SO", 15: "GND", 16: "GND", 17: "GND", 18: "MCP_RESET_N", 19: None, 20: None,
})
mcp_nets.update({number + 21: f"A{number}" for number in range(8)})
add_component("U8", "MCP23S17", "MCP23S17-E/SP", 254.0, 482.6, mcp_nets, footprint="Package_DIP:DIP-28_W7.62mm")
add_component("Q1", "NPN", "2N3904 MCP RESET", 355.6, 482.6, {1: "GND", 2: "MCP_RESET_BASE", 3: "MCP_RESET_N"}, footprint="Package_TO_SOT_THT:TO-92_Inline")
add_component("RN1", "RN8", "8x10k A0-A7 pull-up", 152.4, 533.4, {1: "+5V", **{number + 2: f"A{number}" for number in range(8)}})
add_component("RN2", "RN8", "8x10k A8-A15 pull-up", 254.0, 533.4, {1: "+5V", **{number + 2: f"A{number + 8}" for number in range(8)}})
add_component("RN3", "RN8", "8x10k Pico D0-D7 pull-down", 914.4, 355.6, {1: "GND", **{number + 2: f"PICO_D{number}" for number in range(8)}})

up_nets = {1: "+5V", 10: "GND", 19: "DATA_UP_OE_N", 20: "+5V"}
down_nets = {1: "GND", 10: "GND", 19: "DATA_DOWN_OE_N", 20: "+3V3"}
for bit in range(8):
    up_nets[2 + bit] = f"PICO_D{bit}"
    up_nets[18 - bit] = f"D{bit}"
    down_nets[2 + bit] = f"PICO_D{bit}"
    down_nets[18 - bit] = f"D{bit}"
add_component("U9", "AHCT245", "SN74AHCT245N Pico-to-bus", 812.8, 304.8, up_nets, footprint="Package_DIP:DIP-20_W7.62mm")
add_component("U10", "LVC245", "SN74LVC245AN bus-to-Pico", 914.4, 304.8, down_nets, footprint="Package_DIP:DIP-20_W7.62mm")

pico_nets = {
    1: "PICO_BUSACK_N", 2: "PICO_IORQ_N", 3: "GND", 4: "PICO_CLK",
    5: "@RESET_JUNCTION", 6: "PICO_BUSREQ_N", 7: "PICO_CE_N", 8: "GND",
    9: "DATA_DIR", 10: "DATA_ENABLE", 11: None, 12: "ADDR_ENABLE", 13: "GND",
    14: "PICO_D0", 15: "PICO_D1", 16: "PICO_D2", 17: "PICO_D3", 18: "GND",
    19: "PICO_D4", 20: "PICO_D5", 21: "PICO_D6", 22: "PICO_D7", 23: "GND",
    24: "PICO_SPI_SCK", 25: "PICO_SPI_MOSI", 26: "PICO_SPI_MISO",
    27: "PICO_SPI_CS_N", 28: "GND", 29: "PICO_WE_N", 30: None,
    31: "PICO_OE_N", 32: "PICO_RD_N", 33: "GND", 34: "PICO_WR_N",
    35: None, 36: "+3V3", 37: None, 38: "GND", 39: "VSYS", 40: None,
}
add_component("A1", "PICO2", "Raspberry Pi Pico 2 W", 863.6, 152.4, pico_nets, footprint="Module:RaspberryPi_Pico_Common_THT")

add_component("J1", "SUPPLY", "REGULATED 5V INPUT", 50.8, 50.8, {1: "+5V", 2: "GND"})
add_component("D1", "DIODE", "1N5819", 127.0, 50.8, {1: "VSYS", 2: "+5V"}, footprint="Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal")
add_component("#FLG01", "PWR_FLAG", "PWR_FLAG", 203.2, 50.8, {1: "VSYS"})
add_component("TP1", "TESTPOINT", "M1# TEST", 482.6, 50.8, {1: "M1_N"}, footprint="TestPoint:TestPoint_Loop_D2.60mm_Drill0.9mm_Beaded")


PULLS = [
    ("BUSREQ_N", "+5V"), ("BUSACK_N", "+5V"), ("MREQ_N", "+5V"),
    ("IORQ_N", "+5V"), ("RD_N", "+5V"), ("WR_N", "+5V"), ("MCP_SO", "+5V"),
    ("SRAM_CE_N", "+5V"), ("SRAM_OE_N", "+5V"), ("SRAM_WE_N", "+5V"),
    ("WAIT_N", "+5V"), ("INT_N", "+5V"), ("NMI_N", "+5V"),
    ("SRAM_WE_PRE_N", "+5V"), ("SRAM_OE_PRE_N", "+5V"), ("SRAM_CE_PRE_N", "+5V"),
    ("PICO_BUSREQ_N", "+3V3"), ("PICO_CE_N", "+3V3"),
    ("PICO_SPI_CS_N", "+3V3"), ("PICO_WE_N", "+3V3"), ("PICO_OE_N", "+3V3"),
    ("PICO_CLK", "GND"), ("RESET_N", "GND"), ("DATA_ENABLE", "GND"), ("DATA_DIR", "GND"),
    ("ADDR_ENABLE", "GND"), ("PICO_SPI_SCK", "GND"), ("PICO_SPI_MOSI", "GND"),
]
if len(PULLS) != 28:
    raise AssertionError(f"expected 28 startup resistors, got {len(PULLS)}")
for index, (signal, rail) in enumerate(PULLS, start=1):
    row = (index - 1) // 15
    column = (index - 1) % 15
    add_component(
        f"R{index}", "RESISTOR", f"10k {signal}",
        50.8 + column * 71.12, 660.4 + row * 50.8,
        {1: signal, 2: rail},
        footprint="Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
    )
add_component("R29", "RESISTOR", "4.7k MCP reset base", 50.8, 736.6, {1: "MCP_RESET_DRIVE", 2: "MCP_RESET_BASE"}, footprint="Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal")
add_component("R30", "RESISTOR", "47k MCP reset base pull-down", 127.0, 736.6, {1: "MCP_RESET_BASE", 2: "GND"}, footprint="Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal")
add_component("R31", "RESISTOR", "10k MCP RESET# pull-up", 203.2, 736.6, {1: "MCP_RESET_N", 2: "+5V"}, footprint="Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal")


CAPACITORS = [
    ("100n U1 Z80", "+5V"), ("100n U2 SRAM", "+5V"),
    ("100n U3 GAL", "+5V"), ("100n U4 AHCT244", "+5V"),
    ("100n U7 LVC244", "+3V3"), ("100n U8 MCP", "+5V"),
    ("100n U9 AHCT245", "+5V"), ("100n U10 LVC245", "+3V3"),
    ("22u Memory Board", "+5V"), ("22u Core Board", "+5V"),
    ("22u Peripheral Board", "+5V"), ("100u Supply Entry", "+5V"),
]
if sum(value.startswith("100n") for value, _ in CAPACITORS) != 8:
    raise AssertionError("decoupling count must remain 8")
for index, (value, rail) in enumerate(CAPACITORS, start=1):
    add_component(
        f"C{index}", "CAPACITOR", value,
        50.8 + (index - 1) * 76.2, 787.4,
        {1: rail, 2: "GND"},
        footprint="Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
    )


BUS_ROUTES = [
    ("ADDRESS", [f"A{bit}" for bit in range(16)], [(220.98, 208.28), (439.42, 208.28)]),
    ("DATA", [f"D{bit}" for bit in range(8)], [(220.98, 228.6), (439.42, 228.6)]),
    ("CPU_TO_GAL", ["BUSACK_N", "MREQ_N", "RD_N", "WR_N"], [(220.98, 299.72), (439.42, 299.72)]),
    ("GAL_TO_HCT", ["SRAM_WE_PRE_N", "SRAM_OE_PRE_N", "SRAM_CE_PRE_N"], [(220.98, 327.66), (439.42, 327.66)]),
    ("HCT_TO_SRAM", ["SRAM_WE_N", "SRAM_OE_N", "SRAM_CE_N"], [(220.98, 355.6), (439.42, 355.6)]),
    ("PICO_TO_GAL", ["PICO_CE_N", "PICO_OE_N", "PICO_WE_N"], [(220.98, 109.22), (779.78, 109.22)]),
    ("HCT_TO_CPU", ["Z80_CLK", "BUSREQ_N"], [(457.2, 261.62), (558.8, 261.62)]),
    ("PICO_DATA", [f"PICO_D{bit}" for bit in range(8)], [(617.22, 208.28), (779.78, 208.28)]),
    ("PICO_TO_HCT", ["PICO_CLK", "PICO_BUSREQ_N", "PICO_SPI_CS_N", "PICO_SPI_SCK", "PICO_SPI_MOSI"], [(617.22, 279.4), (779.78, 279.4)]),
    ("HCT_TO_MCP", ["MCP_CS_N", "MCP_SCK", "MCP_SI"], [(220.98, 584.2), (558.8, 584.2)]),
    ("CPU_MONITOR", ["BUSACK_N", "IORQ_N", "RD_N", "WR_N"], [(617.22, 375.92), (779.78, 375.92)]),
    ("PICO_MONITOR", ["PICO_BUSACK_N", "PICO_IORQ_N", "PICO_RD_N", "PICO_WR_N", "PICO_SPI_MISO"], [(617.22, 403.86), (779.78, 403.86)]),
    ("ADDRESS_RESET", ["ADDR_ENABLE", "MCP_RESET_DRIVE", "MCP_RESET_N"], [(220.98, 431.8), (779.78, 431.8)]),
    ("DATA_CONTROL", ["DATA_DIR", "DATA_ENABLE", "DATA_UP_OE_N", "DATA_DOWN_OE_N"], [(220.98, 457.2), (779.78, 457.2)]),
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


reset_points = [pin_points[("U3", "1")], pin_points[("U1", "26")], pin_points[("A1", "5")]]
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


for text, x in (("MEMORY BOARD", 152.4), ("CORE BOARD", 508.0), ("PERIPHERAL BOARD", 863.6)):
    schematic.texts.append(Text(
        text=text,
        position=pos(x, 91.44, 0),
        effects=Effects(font=Font(height=2.54, width=2.54, bold=True)),
        uuid=uid(f"text:{text}"),
    ))
schematic.texts.extend([
    Text(
        text="Named KiCad buses show complete grouped routes; member pin stubs carry exact net labels. RESET_N is an explicit Pico / Z80 / GAL three-way wire junction.",
        position=pos(508.0, 38.1, 0),
        effects=effects(1.27),
        uuid=uid("text:reset-note"),
    ),
    Text(
        text="R1-R16: 5V fail-safe pulls. R17-R21: 3V3 pulls. R22-R28: GPIO pull-downs. R29-R31: MCP reset transistor bias.",
        position=pos(50.8, 622.3, 0),
        effects=effects(1.27),
        uuid=uid("text:pull-note"),
    ),
])


required_endpoints = {
    "A0": {"U1.30", "U2.12", "U8.21", "RN1.2"},
    "A8": {"U1.38", "U2.27", "U8.1", "RN2.2"},
    "D0": {"U1.14", "U2.13", "U9.18", "U10.18"},
    "PICO_D0": {"A1.14", "U9.2", "U10.2", "RN3.2"},
    "DATA_ENABLE": {"A1.10", "R24.1", "U3.9"},
    "DATA_DIR": {"A1.9", "R25.1", "U3.11"},
    "DATA_UP_OE_N": {"U3.17", "U9.19"},
    "DATA_DOWN_OE_N": {"U3.18", "U10.19"},
    "ADDR_ENABLE": {"A1.12", "R26.1", "U3.10"},
    "MCP_RESET_DRIVE": {"U3.19", "R29.1"},
    "MCP_RESET_BASE": {"Q1.2", "R29.2", "R30.1"},
    "MCP_RESET_N": {"Q1.3", "R31.1", "U8.18"},
    "RESET_N": {"U1.26", "U3.1", "A1.5", "R23.1"},
}
for net, required in required_endpoints.items():
    actual = set(net_endpoints.get(net, []))
    if not required <= actual:
        raise AssertionError(f"{net} missing endpoints: {sorted(required - actual)}")

physical_references = sorted(reference for reference in references if not reference.startswith("#"))
erc_references = sorted(reference for reference in references if reference.startswith("#"))
manifest = {
    "generated_by": "scripts/build-kicad-schematic.py",
    "schematic": str(SCHEMATIC_PATH.relative_to(REPO_ROOT)),
    "component_count": len(physical_references),
    "components": physical_references,
    "erc_symbols": erc_references,
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