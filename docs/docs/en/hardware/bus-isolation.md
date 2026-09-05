# 5. Transceiver Operating Modes & Isolation Tables

The data transceivers isolate the Pico from the data bus during standard
execution. The MCP23S17 uses reset-based isolation on the address bus instead.
Both prevent bus contention: two output drivers trying to force opposite
voltages onto the same wire. Treat bus ownership like exclusive write access,
but remember that a hardware conflict can damage parts, not just corrupt data.

## 5.1 All-PDIP Data-Bus Translation and Interlock

No single PDIP part provides an SN74LVC8T245-equivalent combination of
dual supplies, bidirectional translation, deterministic direction,
three-state isolation, and partial-power-down safety. The breadboard
design therefore uses two fixed-direction transceivers:

- **SN74AHCT245N (5 V):** A port is Pico D0-D7, B port is the 5 V bus,
  and DIR pin 1 is tied HIGH. Its TTL-compatible A inputs accept 3.3 V
  and its B outputs provide full 5 V CMOS levels.
- **SN74LVC245AN (3.3 V):** A port is Pico D0-D7, B port is the 5 V bus,
  and DIR pin 1 is tied LOW. Its B inputs tolerate 5.5 V, A outputs stay
  in the Pico domain, and `Ioff` prevents back-powering while 3.3 V is
  absent.

Every corresponding AHCT245, LVC245, Z84C00, and SRAM data pin below is a tap
on one common D0-D7 trunk. The separate chip-pair views do not define separate
nets or a series path through the Z80 or SRAM.

The complete bidirectional data-path wiring is installed in
[Phase 5](../implementation/phase-5-data-bus.md#wiring-bidirectional-data-path).

<template id="phase-5-data-wiring">

### Pico 2 W to SN74AHCT245

```mermaid
block-beta
  columns 2
  P0["Pico D0 - GP10"] U0["AHCT245 A1 - pin 2"]
  P1["Pico D1 - GP11"] U1["AHCT245 A2 - pin 3"]
  P2["Pico D2 - GP12"] U2["AHCT245 A3 - pin 4"]
  P3["Pico D3 - GP13"] U3["AHCT245 A4 - pin 5"]
  P4["Pico D4 - GP14"] U4["AHCT245 A5 - pin 6"]
  P5["Pico D5 - GP15"] U5["AHCT245 A6 - pin 7"]
  P6["Pico D6 - GP16"] U6["AHCT245 A7 - pin 8"]
  P7["Pico D7 - GP17"] U7["AHCT245 A8 - pin 9"]
  P0 --> U0
  P1 --> U1
  P2 --> U2
  P3 --> U3
  P4 --> U4
  P5 --> U5
  P6 --> U6
  P7 --> U7
```

### SN74AHCT245 to Z84C00

```mermaid
block-beta
  columns 2
  U0["AHCT245 B1 - pin 18"] Z0["Z80 D0 - pin 14"]
  U1["AHCT245 B2 - pin 17"] Z1["Z80 D1 - pin 15"]
  U2["AHCT245 B3 - pin 16"] Z2["Z80 D2 - pin 12"]
  U3["AHCT245 B4 - pin 15"] Z3["Z80 D3 - pin 8"]
  U4["AHCT245 B5 - pin 14"] Z4["Z80 D4 - pin 7"]
  U5["AHCT245 B6 - pin 13"] Z5["Z80 D5 - pin 9"]
  U6["AHCT245 B7 - pin 12"] Z6["Z80 D6 - pin 10"]
  U7["AHCT245 B8 - pin 11"] Z7["Z80 D7 - pin 13"]
  U0 --> Z0
  U1 --> Z1
  U2 --> Z2
  U3 --> Z3
  U4 --> Z4
  U5 --> Z5
  U6 --> Z6
  U7 --> Z7
```

### SN74AHCT245 to AS6C1008 SRAM

```mermaid
block-beta
  columns 2
  U0["AHCT245 B1 - pin 18"] R0["SRAM I/O0 - pin 13"]
  U1["AHCT245 B2 - pin 17"] R1["SRAM I/O1 - pin 14"]
  U2["AHCT245 B3 - pin 16"] R2["SRAM I/O2 - pin 15"]
  U3["AHCT245 B4 - pin 15"] R3["SRAM I/O3 - pin 17"]
  U4["AHCT245 B5 - pin 14"] R4["SRAM I/O4 - pin 18"]
  U5["AHCT245 B6 - pin 13"] R5["SRAM I/O5 - pin 19"]
  U6["AHCT245 B7 - pin 12"] R6["SRAM I/O6 - pin 20"]
  U7["AHCT245 B8 - pin 11"] R7["SRAM I/O7 - pin 21"]
  U0 --> R0
  U1 --> R1
  U2 --> R2
  U3 --> R3
  U4 --> R4
  U5 --> R5
  U6 --> R6
  U7 --> R7
```

### Z84C00 to SN74LVC245

```mermaid
block-beta
  columns 2
  Z0["Z80 D0 - pin 14"] L0["LVC245 B1 - pin 18"]
  Z1["Z80 D1 - pin 15"] L1["LVC245 B2 - pin 17"]
  Z2["Z80 D2 - pin 12"] L2["LVC245 B3 - pin 16"]
  Z3["Z80 D3 - pin 8"] L3["LVC245 B4 - pin 15"]
  Z4["Z80 D4 - pin 7"] L4["LVC245 B5 - pin 14"]
  Z5["Z80 D5 - pin 9"] L5["LVC245 B6 - pin 13"]
  Z6["Z80 D6 - pin 10"] L6["LVC245 B7 - pin 12"]
  Z7["Z80 D7 - pin 13"] L7["LVC245 B8 - pin 11"]
  Z0 --> L0
  Z1 --> L1
  Z2 --> L2
  Z3 --> L3
  Z4 --> L4
  Z5 --> L5
  Z6 --> L6
  Z7 --> L7
```

### AS6C1008 SRAM to SN74LVC245

```mermaid
block-beta
  columns 2
  R0["SRAM I/O0 - pin 13"] L0["LVC245 B1 - pin 18"]
  R1["SRAM I/O1 - pin 14"] L1["LVC245 B2 - pin 17"]
  R2["SRAM I/O2 - pin 15"] L2["LVC245 B3 - pin 16"]
  R3["SRAM I/O3 - pin 17"] L3["LVC245 B4 - pin 15"]
  R4["SRAM I/O4 - pin 18"] L4["LVC245 B5 - pin 14"]
  R5["SRAM I/O5 - pin 19"] L5["LVC245 B6 - pin 13"]
  R6["SRAM I/O6 - pin 20"] L6["LVC245 B7 - pin 12"]
  R7["SRAM I/O7 - pin 21"] L7["LVC245 B8 - pin 11"]
  R0 --> L0
  R1 --> L1
  R2 --> L2
  R3 --> L3
  R4 --> L4
  R5 --> L5
  R6 --> L6
  R7 --> L7
```

### SN74LVC245 to Pico 2 W

```mermaid
block-beta
  columns 2
  L0["LVC245 A1 - pin 2"] P0["Pico D0 - GP10"]
  L1["LVC245 A2 - pin 3"] P1["Pico D1 - GP11"]
  L2["LVC245 A3 - pin 4"] P2["Pico D2 - GP12"]
  L3["LVC245 A4 - pin 5"] P3["Pico D3 - GP13"]
  L4["LVC245 A5 - pin 6"] P4["Pico D4 - GP14"]
  L5["LVC245 A6 - pin 7"] P5["Pico D5 - GP15"]
  L6["LVC245 A7 - pin 8"] P6["Pico D6 - GP16"]
  L7["LVC245 A8 - pin 9"] P7["Pico D7 - GP17"]
  L0 --> P0
  L1 --> P1
  L2 --> P2
  L3 --> P3
  L4 --> P4
  L5 --> P5
  L6 --> P6
  L7 --> P7
```

Connect both pin 10s to GND. Connect AHCT pin 20 to 5 V and LVC pin
20 to 3.3 V. Each device needs its own local 100 nF capacitor.

The existing ATF22V10 provides the hardware interlock using spare pins
and product terms. Connect GP7 DATA_ENABLE to GAL pin 9 and GP6
DATA_DIR to pin 11. GAL pin 17 drives AHCT245 OE# pin 19; pin 18 drives
LVC245 OE# pin 19. The two equations are documented in
[SRAM control-source arbitration](pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc).
No 5 V output drives GP6/GP7: they are GAL inputs with 10 kOhm
pull-downs. During Pico power-off both inputs read LOW, so both GAL OE#
outputs are HIGH. The LVC245's `Ioff` specification protects its
3.3 V-powered side while GAL pin 18 remains at a 5 V-domain HIGH.

| System state | DATA_ENABLE GP7 | DATA_DIR GP6 | AHCT OE# | LVC OE# | Result |
|----|----:|----:|----:|----:|----|
| Isolated / run | 0 | X | 1 | 1 | Both Pico data paths high-impedance |
| DMA write | 1 | 1 | 0 | 1 | Pico → AHCT245 → 5 V bus |
| Readback / OUT trap | 1 | 0 | 1 | 0 | 5 V bus → LVC245 → Pico |

Firmware always drives DATA_ENABLE LOW, waits, changes DATA_DIR, waits,
and only then drives DATA_ENABLE HIGH. The GAL truth table also
makes simultaneous enables impossible for every static GP6/GP7 state.
Do not substitute TXS0108E, TXB0108, BSS138, or resistor-divider
breakouts: their automatic/pass-device behavior and loading assumptions
are not equivalent to this controlled, multi-load push-pull bus.

phase-5-data-wiring-end</template>

## 5.2 Direct MCP23S17 Address-Bus Modes

| System state | ADDR_ENABLE GP9 | MCP RESET# | IODIRA/B | Functional role |
|----|----:|----:|----|----|
| **DMA injection** | 1 | 1 | `0x00/0x00` after OLAT preload | MCP drives A0-A15 |
| **Trap address read** | 1 | 1 | `0xFF/0xFF` | MCP samples the frozen Z80 address |
| **Active execution / isolated** | 0 | 0 | Reset default `0xFF/0xFF` | MCP pins are inputs; Z80 owns A0-A15 |

Always assert ADDR_ENABLE LOW before releasing the CPU. Releasing MCP
reset is not itself permission to drive: firmware must preload OLAT and
hold Z80 RESET# or BUSACK# before writing IODIR outputs.

## 5.3 SN74LVC244AN 5 V-to-3.3 V Input Buffer

The RP2350's GP0-GP25 are 5 V-tolerant FT pads, but the 5.5 V rating
applies only while IOVDD is powered at 3.3 V; with IOVDD at 0 V their
absolute maximum is 3.63 V. GP26-GP29 are the QFN-60 package's
ADC-capable pads and are not FT at all. Buffering all five incoming
5 V signals therefore avoids a power-sequencing constraint and keeps
every Pico GPIO within its normal 3.3 V domain. Power the SN74LVC244AN
from the Pico 3.3 V rail. Its inputs accept up to 5.5 V and its `Ioff`
specification protects both sides when VCC is 0 V. Tie both output
enables (pins 1 and 19) to GND: every buffered signal -- BUSACK#,
IORQ#, RD#, WR#, and MCP SO -- must always be readable, and none of
them share a Pico GPIO with any other driver, so neither output enable
needs gating. Wire the channels as follows; tie every unused input to
GND and leave unused outputs open.

The complete input-buffer wiring is installed in
[Phase 3](../implementation/phase-3-address-generator.md#wiring-spi-and-mcp23s17-reset).

<template id="phase-3-input-buffer-wiring">

### Z84C00 to SN74LVC244

```mermaid
block-beta
  columns 2
  BACK["Z80 BUSACK# - pin 23"] B1["LVC244 1A1 - pin 2"]
  IORQ["Z80 IORQ# - pin 20"] B2["LVC244 1A2 - pin 4"]
  RD["Z80 RD# - pin 21"] B3["LVC244 1A3 - pin 6"]
  WR["Z80 WR# - pin 22"] B4["LVC244 1A4 - pin 8"]
  BACK --> B1
  IORQ --> B2
  RD --> B3
  WR --> B4
```

### MCP23S17 to SN74LVC244

```mermaid
block-beta
  columns 2
  SO["MCP23S17 SO - pin 14"] B5["LVC244 2A1 - pin 11"]
  SO --> B5
```

### SN74LVC244 to Pico 2 W

```mermaid
block-beta
  columns 2
  B1["LVC244 1Y1 - pin 18"] P0["Pico BUSACK# - GP0"]
  B2["LVC244 1Y2 - pin 16"] P1["Pico IORQ# - GP1"]
  B3["LVC244 1Y3 - pin 14"] P27["Pico RD# - GP27"]
  B4["LVC244 1Y4 - pin 12"] P28["Pico WR# - GP28"]
  B5["LVC244 2Y1 - pin 9"] P20["Pico MISO - GP20"]
  B1 --> P0
  B2 --> P1
  B3 --> P27
  B4 --> P28
  B5 --> P20
```

VCC (pin 20) connects to 3.3 V and GND (pin 10) to common ground. The
[5 V-side pull-ups](../implementation/phase-0-power.md#passive-component-installation) keep every input defined when its
source is absent or high-impedance. Tie unused inputs 2A2/2A3/2A4
(pins 13/15/17) to GND and leave outputs 2Y2/2Y3/2Y4 (pins 7/5/3) open.

phase-3-input-buffer-wiring-end</template>

## 5.4 Implementation Wiring Index

Use the pair-specific diagrams in the phase where each path is installed:

- [Pico wiring](../implementation/phase-1-supervisor.md#wiring-pico-2-w)
- [GAL and AHCT244 wiring](../implementation/phase-2-buffer-clock.md#wiring-gal-and-output-buffer)
- [MCP23S17, address-trunk, and input-buffer wiring](../implementation/phase-3-address-generator.md#wiring-spi-and-mcp23s17-reset)
- [Bidirectional data-path wiring](../implementation/phase-5-data-bus.md#wiring-bidirectional-data-path)
- [SRAM socket wiring](../implementation/phase-6-sram.md#wiring-sram-socket)

The matching signal names across those diagrams identify common physical bus
trunks; they do not imply that a bus passes serially through each chip.
