# 2. 16-Bit Address Expansion Interface: MCP23S17-E/SP

The MCP23S17 acts as the dedicated 16-bit I/O expander connecting the
Pico 2 W's SPI bus to the shared 5 V address bus. During DMA block
injection, it drives the target SRAM locations. During an active I/O
trap, its GPIO ports remain inputs, allowing the MCP23S17 to monitor the
address bus states driven by the frozen Z80 CPU.

## 2.0 SPI and reset interconnects

The complete SPI and reset wiring is installed in
[Phase 3](../implementation/phase-3-address-generator.md#wiring-spi-and-mcp23s17-reset).

<template id="phase-3-spi-reset-wiring">

### Pico 2 W to SN74AHCT244

```mermaid
block-beta
  columns 2
  PCS["Pico CS# - GP21"] BCS["AHCT244 1A3 - pin 6"]
  PSCK["Pico SCK - GP18"] BSCK["AHCT244 1A4 - pin 8"]
  PMOSI["Pico MOSI - GP19"] BMOSI["AHCT244 2A1 - pin 11"]
  PCS --> BCS
  PSCK --> BSCK
  PMOSI --> BMOSI
```

### SN74AHCT244 to MCP23S17

```mermaid
block-beta
  columns 2
  BCS["AHCT244 1Y3 - pin 14"] MCS["MCP23S17 CS# - pin 11"]
  BSCK["AHCT244 1Y4 - pin 12"] MSCK["MCP23S17 SCK - pin 12"]
  BMOSI["AHCT244 2Y1 - pin 9"] MSI["MCP23S17 SI - pin 13"]
  BCS --> MCS
  BSCK --> MSCK
  BMOSI --> MSI
```

### MCP23S17 to SN74LVC244

```mermaid
block-beta
  columns 2
  MSO["MCP23S17 SO - pin 14"] BSO["LVC244 2A1 - pin 11"]
  MSO --> BSO
```

### SN74LVC244 to Pico 2 W

```mermaid
block-beta
  columns 2
  BSO["LVC244 2Y1 - pin 9"] PMISO["Pico MISO - GP20"]
  BSO --> PMISO
```

Fit the [specified pull-up](../implementation/phase-0-power.md#passive-component-installation) on SO
because it is high-impedance while CS# is HIGH.

### Pico 2 W to ATF22V10

```mermaid
block-beta
  columns 2
  PEN["Pico ADDR_ENABLE - GP9"] GEN["ATF22V10 ADDR_ENABLE - pin 10"]
  PEN --> GEN
```

### ATF22V10 to Q1

```mermaid
block-beta
  columns 2
  GEN["ATF22V10 MCP_RESET_DRIVE - pin 19"] Q1["Q1 base<br/>through 4.7 kOhm"]
  GEN --> Q1
```

### Q1 to MCP23S17

```mermaid
block-beta
  columns 2
  Q1["Q1 collector<br/>reset pull-down"] MRST["MCP23S17 RESET# - pin 18"]
  Q1 --> MRST
```

Tie MCP23S17 A0, A1, and A2 (pins 15-17) directly to common GND. This selects
hardware address `000`, matching the fixed
`0x40`/`0x41` firmware opcodes regardless of IOCON.HAEN. RESET# has a 10 kOhm
pull-up to 5 V; Q1 holds it LOW outside address access, returning every MCP GPIO
to input mode.

[Microchip MCP23017/MCP23S17 16-Bit I/O Expander with Serial Interface Datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-MCP23S17-16-Bit-IO-Expander-with-Serial-Interface-DS20001952.pdf)

phase-3-spi-reset-wiring-end</template>

## 2.1 MCP23S17 Address-Bus Interconnects

The MCP23S17 connects directly to the shared address bus: GPA0-GPA7 to
A0-A7 and GPB0-GPB7 to A8-A15. Fit one 8x10 kOhm bussed pull-up network
per byte, with each common pin at 5 V. The Z84C00 guarantees 4.2 V at
its light-load VOH2 point against the MCP's 4.0 V input minimum; the
external pull-ups improve the static HIGH level and add at most about
0.5 mA load per LOW bit. MCP GPIO outputs guarantee at least 4.3 V at
5 V, exceeding the SRAM's 3.5 V input minimum.

Hardware isolation comes from RESET#, not a bus transceiver. GP9
ADDR_ENABLE feeds GAL pin 10. When LOW, GAL pin 19 drives Q1 and holds
MCP RESET# LOW, forcing IODIRA/IODIRB to their all-input reset values.
When HIGH, Q1 releases RESET#; firmware waits, then programs OLAT before
changing IODIR to outputs. It reasserts reset before releasing the Z80.
GP9 has a 10 kOhm pull-down, so Pico reset or power loss fails closed.

Every corresponding MCP23S17, Z84C00, and SRAM address pin below is a tap on
one common pulled-up address trunk. The separate chip-pair views do not imply
independent point-to-point nets or a series path through either device.

The complete address-trunk wiring is installed in
[Phase 3](../implementation/phase-3-address-generator.md#wiring-spi-and-mcp23s17-reset).

<template id="phase-3-address-trunk-wiring">

### MCP23S17 to Z84C00: A0-A7

```mermaid
block-beta
  columns 2
  GPA0["MCP GPA0 - pin 21"] A0["Z80 A0 - pin 30"]
  GPA1["MCP GPA1 - pin 22"] A1["Z80 A1 - pin 31"]
  GPA2["MCP GPA2 - pin 23"] A2["Z80 A2 - pin 32"]
  GPA3["MCP GPA3 - pin 24"] A3["Z80 A3 - pin 33"]
  GPA4["MCP GPA4 - pin 25"] A4["Z80 A4 - pin 34"]
  GPA5["MCP GPA5 - pin 26"] A5["Z80 A5 - pin 35"]
  GPA6["MCP GPA6 - pin 27"] A6["Z80 A6 - pin 36"]
  GPA7["MCP GPA7 - pin 28"] A7["Z80 A7 - pin 37"]
  GPA0 <--> A0
  GPA1 <--> A1
  GPA2 <--> A2
  GPA3 <--> A3
  GPA4 <--> A4
  GPA5 <--> A5
  GPA6 <--> A6
  GPA7 <--> A7
```

### MCP23S17 to Z84C00: A8-A15

```mermaid
block-beta
  columns 2
  GPB0["MCP GPB0 - pin 1"] A8["Z80 A8 - pin 38"]
  GPB1["MCP GPB1 - pin 2"] A9["Z80 A9 - pin 39"]
  GPB2["MCP GPB2 - pin 3"] A10["Z80 A10 - pin 40"]
  GPB3["MCP GPB3 - pin 4"] A11["Z80 A11 - pin 1"]
  GPB4["MCP GPB4 - pin 5"] A12["Z80 A12 - pin 2"]
  GPB5["MCP GPB5 - pin 6"] A13["Z80 A13 - pin 3"]
  GPB6["MCP GPB6 - pin 7"] A14["Z80 A14 - pin 4"]
  GPB7["MCP GPB7 - pin 8"] A15["Z80 A15 - pin 5"]
  GPB0 <--> A8
  GPB1 <--> A9
  GPB2 <--> A10
  GPB3 <--> A11
  GPB4 <--> A12
  GPB5 <--> A13
  GPB6 <--> A14
  GPB7 <--> A15
```

### MCP23S17 to AS6C1008 SRAM: A0-A7

```mermaid
block-beta
  columns 2
  GPA0["MCP GPA0 - pin 21"] A0["SRAM A0 - pin 12"]
  GPA1["MCP GPA1 - pin 22"] A1["SRAM A1 - pin 11"]
  GPA2["MCP GPA2 - pin 23"] A2["SRAM A2 - pin 10"]
  GPA3["MCP GPA3 - pin 24"] A3["SRAM A3 - pin 9"]
  GPA4["MCP GPA4 - pin 25"] A4["SRAM A4 - pin 8"]
  GPA5["MCP GPA5 - pin 26"] A5["SRAM A5 - pin 7"]
  GPA6["MCP GPA6 - pin 27"] A6["SRAM A6 - pin 6"]
  GPA7["MCP GPA7 - pin 28"] A7["SRAM A7 - pin 5"]
  GPA0 --> A0
  GPA1 --> A1
  GPA2 --> A2
  GPA3 --> A3
  GPA4 --> A4
  GPA5 --> A5
  GPA6 --> A6
  GPA7 --> A7
```

### MCP23S17 to AS6C1008 SRAM: A8-A15

```mermaid
block-beta
  columns 2
  GPB0["MCP GPB0 - pin 1"] A8["SRAM A8 - pin 27"]
  GPB1["MCP GPB1 - pin 2"] A9["SRAM A9 - pin 26"]
  GPB2["MCP GPB2 - pin 3"] A10["SRAM A10 - pin 23"]
  GPB3["MCP GPB3 - pin 4"] A11["SRAM A11 - pin 25"]
  GPB4["MCP GPB4 - pin 5"] A12["SRAM A12 - pin 4"]
  GPB5["MCP GPB5 - pin 6"] A13["SRAM A13 - pin 28"]
  GPB6["MCP GPB6 - pin 7"] A14["SRAM A14 - pin 3"]
  GPB7["MCP GPB7 - pin 8"] A15["SRAM A15 - pin 31"]
  GPB0 --> A8
  GPB1 --> A9
  GPB2 --> A10
  GPB3 --> A11
  GPB4 --> A12
  GPB5 --> A13
  GPB6 --> A14
  GPB7 --> A15
```

phase-3-address-trunk-wiring-end</template>
