# 1. Reference Pin Mapping & Logic Domain Verification

This architecture leverages the fully static nature of the Z84C0020PEC
CMOS CPU. The Pico controls the clock and uses the Z80 BUSREQ#/
BUSACK# handshake for DMA ownership. During RESET, the address and data
buses float and control outputs are inactive. During BUSACK#, A0-A15,
D0-D7, MREQ#, IORQ#, RD#, and WR# float while BUSACK# remains actively
driven LOW; M1#, RFSH#, and HALT# are not part of the floated bus set.

## 1.0 Raspberry Pi Pico 2 Header Pin Map

GPIO numbers in the firmware are not physical header numbers. Use this
table for construction; the ranges preserve ascending bit order.

| Pico function | GPIO | Physical header pin | Connection |
|----|----:|----:|----|
| BUSACK# monitor | GP0 | 1 | SN74LVC244AN 1Y1 |
| IORQ# trap input | GP1 | 2 | SN74LVC244AN 1Y2 |
| Z80 CLK | GP2 | 4 | SN74AHCT244 1A1 pin 2 |
| Z80 RESET# | GP3 | 5 | Z80 pin 26 and ATF22V10 pin 1, direct 3.3 V logic |
| Z80 BUSREQ# | GP4 | 6 | SN74AHCT244 1A2 pin 4 |
| SRAM CE# | GP5 | 7 | ATF22V10 pin 7 |
| Data DIR / ENABLE | GP6 / GP7 | 9 / 10 | ATF22V10 pins 11 / 9; GAL pins 17 / 18 generate mutually exclusive data OE# signals |
| Unused | GP8 | 11 | Leave open; no address transceivers are fitted |
| Address enable | GP9 | 12 | ATF22V10 pin 10; HIGH releases MCP RESET# through GAL pin 19 and Q1 |
| D0-D3 | GP10-GP13 | 14-17 | SN74AHCT245N and SN74LVC245AN A1-A4 pins 2-5 |
| D4-D7 | GP14-GP17 | 19-22 | SN74AHCT245N and SN74LVC245AN A5-A8 pins 6-9 |
| SPI SCK / MOSI / MISO / CS# | GP18-GP21 | 24-27 | AHCT244 1A4 pin 8 / 2A1 pin 11, LVC244 2Y1, AHCT244 1A3 pin 6 |
| SRAM WE# | GP22 | 29 | ATF22V10 pin 3 |
| SRAM OE# | GP26 | 31 | ATF22V10 pin 5 |
| Z80 RD# monitor | GP27 | 32 | SN74LVC244AN 1Y3 |
| Z80 WR# monitor | GP28 | 34 | SN74LVC244AN 1Y4 |
| 3.3 V output | - | 36 | SN74LVC245AN VCC, LVC244 VCC, and 3.3 V pull-ups |
| VSYS | - | 39 | 1N5819 banded cathode; anode to external +5 V |

Connect Pico GND pins 3, 8, 13, 18, 23, 28, and 38, plus AGND pin
33, to common ground with multiple short links. Leave RUN pin 30,
ADC_VREF pin 35, and 3V3_EN pin 37 open. Do not connect the external
5 V rail to VBUS pin 40; USB supplies that node internally.

<table>
<colgroup>
<col style="width: 25%" />
<col style="width: 25%" />
<col style="width: 25%" />
<col style="width: 25%" />
</colgroup>
<thead>
<tr>
<th>Component</th>
<th>Signal Name / Group</th>
<th>Hardware Pin Numbers</th>
<th>Electrical Constraint / Logic Domain</th>
</tr>
</thead>
<tbody>
<tr>
<td rowspan="14"><strong>Z84C0020PEC (CPU)</strong></td>
<td>CLK</td>
<td>Pin 6</td>
<td>Input. Driven by level-shifted 5V CMOS square wave. Clock can be
safely frozen indefinitely in either a HIGH or LOW state.</td>
</tr>
<tr>
<td>IORQ#</td>
<td>Pin 20</td>
<td>Output. Active Low. Buffered to 3.3 V through SN74LVC244AN channel
1A2/1Y2 and monitored by Pico 2 GP1 to trigger clock-stop trapping. The
raw 5 V node also feeds ATF22V10 pin 13 for hardware WAIT generation.</td>
</tr>
<tr>
<td>MREQ#</td>
<td>Pin 19</td>
<td>Output. Active Low, three-state. Connects to ATF22V10 pin 8 for
CPU-owned SRAM cycles; pulled HIGH when the CPU is absent.</td>
</tr>
<tr>
<td>RD#</td>
<td>Pin 21</td>
<td>Output. Active Low. Buffered to 3.3 V through SN74LVC244AN channel
1A3/1Y3 and sampled by Pico 2 GP27 to resolve cycle intent.</td>
</tr>
<tr>
<td>WR#</td>
<td>Pin 22</td>
<td>Output. Active Low. Buffered to 3.3 V through SN74LVC244AN channel
1A4/1Y4 and sampled by Pico 2 GP28 to resolve cycle intent alongside
RD#.</td>
</tr>
<tr>
<td>BUSACK#</td>
<td>Pin 23</td>
<td>Output. Active Low. Feeds ATF22V10 pin 2 for SRAM-source arbitration
([Section 1.2](#12-sram-control-source-arbitration-atf22v10bc)) and is buffered to 3.3 V through SN74LVC244AN
channel 1A1/1Y1 for monitoring at Pico 2 GP0.</td>
</tr>
<tr>
<td>BUSREQ#</td>
<td>Pin 25</td>
<td>Input. Active Low. Level-shifted up to 5V to request DMA
ownership.</td>
</tr>
<tr>
<td>RESET#</td>
<td>Pin 26</td>
<td>Input. Active Low. Driven directly by Pico GP3 at TTL-compatible
3.3 V and also connected to ATF22V10 pin 1 for SRAM-source arbitration.
Hold LOW for at least three full clock cycles.</td>
</tr>
<tr>
<td>M1#</td>
<td>Pin 27</td>
<td>Output. Active Low. Probe point for the Phase 7 opcode-fetch test;
not connected to Pico logic.</td>
</tr>
<tr>
<td>A0 – A15 (Address Bus)</td>
<td>Pins 30 – 40, 1 – 5</td>
<td>5V Tri-state Bus. Driven by CPU during active run, floats during
DMA/RESET.</td>
</tr>
<tr>
<td>D0 – D7 (Data Bus)</td>
<td>Pins 7 – 10, 12 – 15</td>
<td>5V Bi-directional Tri-state Bus. Connected directly to SRAM data bus
pins.</td>
</tr>
<tr>
<td>WAIT#</td>
<td>Pin 24</td>
<td>Input. Active Low. Driven by ATF22V10 pin 20 during I/O trapping and
pulled to +5 V through 10 kOhm so it remains inactive if the GAL is
physically absent.</td>
</tr>
<tr>
<td>INT#</td>
<td>Pin 16</td>
<td>Input. Active Low. Unused; pull to +5V through 10 kOhm to prevent a
spurious interrupt from a floating input.</td>
</tr>
<tr>
<td>NMI#</td>
<td>Pin 17</td>
<td>Input. Active Low, edge-sensed. Unused; pull to +5V through 10 kOhm
to prevent a spurious non-maskable interrupt from a floating input.</td>
</tr>
<tr>
<td rowspan="7"><strong>AS6C1008-55PCN (SRAM)</strong></td>
<td>A0 – A15 (Address lines)</td>
<td>Pins 12-10, 9-7, 6-5, 27-26, 23, 25, 4, 28, 3, 31</td>
<td>5V Input. Connected directly to the 5V Z80 address bus. Driven by
MCP23S17 during DMA.</td>
</tr>
<tr>
<td>A16</td>
<td>Pin 2</td>
<td>5V Input. Tie to GND to select the lower 64KB bank; do not leave
floating.</td>
</tr>
<tr>
<td>D0 – D7 (Data lines)</td>
<td>Pins 13 – 15, 17 – 21</td>
<td>5V Bi-directional Bus. Connected directly to the Z80 data bus.</td>
</tr>
<tr>
<td>WE#</td>
<td>Pin 29</td>
<td>Input. Active Low. Driven by SN74AHCT244 2Y2 pin 7 from the ATF22V10
arbitration output ([Section 1.2](#12-sram-control-source-arbitration-atf22v10bc)): Z80 WR# during CPU ownership, Pico
DMA write signal during DMA ownership.</td>
</tr>
<tr>
<td>OE#</td>
<td>Pin 24</td>
<td>Input. Active Low. Driven by SN74AHCT244 2Y3 pin 5 from the ATF22V10
arbitration output ([Section 1.2](#12-sram-control-source-arbitration-atf22v10bc)): Z80 RD# during CPU ownership, Pico
DMA read signal during DMA ownership.</td>
</tr>
<tr>
<td>CE#</td>
<td>Pin 22</td>
<td>Input. Active Low. Driven by SN74AHCT244 2Y4 pin 3 from the ATF22V10
arbitration output ([Section 1.2](#12-sram-control-source-arbitration-atf22v10bc)): Z80 MREQ# during CPU ownership so
I/O cycles cannot access SRAM, Pico DMA chip-enable signal during DMA
ownership.</td>
</tr>
<tr>
<td>CE2</td>
<td>Pin 30</td>
<td>Input. Active High. Tie permanently to VCC (+5V) to enable the
device through the active-low CE# input.</td>
</tr>
</tbody>
</table>

## 1.1 Z84C0020PEC-to-AS6C1008-55PCN Wiring

The following is the direct run-mode connection verified against the
manufacturers' 40-pin PDIP and 32-pin PDIP pin assignments. The
AS6C1008 is a 128K x 8 device, so A16 must be tied LOW to expose its
lower 64KB as the Z80's address space. CE2 must be tied HIGH, and pin 1
is not connected.

Manufacturer datasheets:

- [Zilog Z84C00 CMOS Z80 CPU Product Specification](https://www.zilog.com/docs/z80/ps0178.pdf)
- [Alliance Memory AS6C1008 128K x 8 Low Power CMOS SRAM Datasheet](https://www.alliancememory.com/wp-content/uploads/AS6C1008_Mar_2023V1.2.pdf)

### Package Pinouts

[![Z84C0020PEC 40-pin PDIP pinout](../images/Z84C0020PEC%20Bus%20Pinout%20Chart.png)](https://www.zilog.com/docs/z80/ps0178.pdf)

[![AS6C1008-55PCN 32-pin PDIP pinout](../images/AS6C1008%20SRAM%20Chip%20Pinout.png)](https://www.alliancememory.com/wp-content/uploads/AS6C1008_Mar_2023V1.2.pdf)

### Address Bus A0-A7

```mermaid
block-beta
  columns 2
  CA0["Z80 A0 - pin 30"] RA0["SRAM A0 - pin 12"]
  CA1["Z80 A1 - pin 31"] RA1["SRAM A1 - pin 11"]
  CA2["Z80 A2 - pin 32"] RA2["SRAM A2 - pin 10"]
  CA3["Z80 A3 - pin 33"] RA3["SRAM A3 - pin 9"]
  CA4["Z80 A4 - pin 34"] RA4["SRAM A4 - pin 8"]
  CA5["Z80 A5 - pin 35"] RA5["SRAM A5 - pin 7"]
  CA6["Z80 A6 - pin 36"] RA6["SRAM A6 - pin 6"]
  CA7["Z80 A7 - pin 37"] RA7["SRAM A7 - pin 5"]
  CA0 --> RA0
  CA1 --> RA1
  CA2 --> RA2
  CA3 --> RA3
  CA4 --> RA4
  CA5 --> RA5
  CA6 --> RA6
  CA7 --> RA7
```

### Address Bus A8-A15

```mermaid
block-beta
  columns 2
  CA8["Z80 A8 - pin 38"] RA8["SRAM A8 - pin 27"]
  CA9["Z80 A9 - pin 39"] RA9["SRAM A9 - pin 26"]
  CA10["Z80 A10 - pin 40"] RA10["SRAM A10 - pin 23"]
  CA11["Z80 A11 - pin 1"] RA11["SRAM A11 - pin 25"]
  CA12["Z80 A12 - pin 2"] RA12["SRAM A12 - pin 4"]
  CA13["Z80 A13 - pin 3"] RA13["SRAM A13 - pin 28"]
  CA14["Z80 A14 - pin 4"] RA14["SRAM A14 - pin 3"]
  CA15["Z80 A15 - pin 5"] RA15["SRAM A15 - pin 31"]
  CA8 --> RA8
  CA9 --> RA9
  CA10 --> RA10
  CA11 --> RA11
  CA12 --> RA12
  CA13 --> RA13
  CA14 --> RA14
  CA15 --> RA15
```

### Data Bus D0-D7

```mermaid
block-beta
  columns 2
  CD0["Z80 D0 - pin 14"] RD0["SRAM I/O0 - pin 13"]
  CD1["Z80 D1 - pin 15"] RD1["SRAM I/O1 - pin 14"]
  CD2["Z80 D2 - pin 12"] RD2["SRAM I/O2 - pin 15"]
  CD3["Z80 D3 - pin 8"] RD3["SRAM I/O3 - pin 17"]
  CD4["Z80 D4 - pin 7"] RD4["SRAM I/O4 - pin 18"]
  CD5["Z80 D5 - pin 9"] RD5["SRAM I/O5 - pin 19"]
  CD6["Z80 D6 - pin 10"] RD6["SRAM I/O6 - pin 20"]
  CD7["Z80 D7 - pin 13"] RD7["SRAM I/O7 - pin 21"]
  CD0 <--> RD0
  CD1 <--> RD1
  CD2 <--> RD2
  CD3 <--> RD3
  CD4 <--> RD4
  CD5 <--> RD5
  CD6 <--> RD6
  CD7 <--> RD7
```

### Memory Control

```mermaid
flowchart TB
  subgraph SOURCES["Ownership and control candidates"]
    direction LR
    OWNER["RESET# + BUSACK#<br/>select active owner"]
    CPU["Z80 controls<br/>MREQ# pin 19<br/>RD# pin 21<br/>WR# pin 22"]
    PICO["Pico DMA controls<br/>CE# / OE# / WE#"]
  end
  OWNER --> GAL["ATF22V10<br/>reset-aware arbitration"]
  CPU --> GAL
  PICO --> GAL
  GAL -->|"TTL pre-buffer CE# / OE# / WE#"| BUFFER["SN74AHCT244<br/>channels 2A2-2A4"]
  BUFFER -->|"5 V controls"| SRAM["AS6C1008-55PCN SRAM<br/>CE# pin 22<br/>OE# pin 24<br/>WE# pin 29"]
```

### Power and Fixed Pins

```mermaid
block-beta
  columns 2
  VCC["Regulated +5V"]
  GND["Common GND"]
  CVCC["CPU VCC - pin 11"]
  CGND["CPU GND - pin 29"]
  RVCC["SRAM VCC - pin 32"]
  RGND["SRAM GND - pin 16"]
  RCE2["SRAM CE2 - pin 30<br/>active HIGH enable"]
  RA16["SRAM A16 - pin 2<br/>selects lower 64KB"]
  RNC["SRAM NC - pin 1<br/>leave open"]
  space
  VCC --> CVCC
  VCC --> RVCC
  VCC --> RCE2
  GND --> CGND
  GND --> RGND
  GND --> RA16
```

> **SRAM control-source arbitration:** MREQ#/RD#/WR# above are the
> CPU-owned run-mode inputs of the ATF22V10 described in
> [Section 1.2](#12-sram-control-source-arbitration-atf22v10bc),
> never directly to the SRAM. The programmed equations select either
> these inputs or the Pico DMA controls; the three results then pass
> through dedicated SN74AHCT244 channels to the SRAM.

## 1.2 SRAM Control-Source Arbitration: ATF22V10B/C

The Z80 and Pico must never drive SRAM CE#, OE#, or WE# simultaneously.
The ATF22V10 implements three combinational 2:1 selectors with a shared
ownership condition:

```text
CPU_OWNS_SRAM = RESET# AND BUSACK#

SRAM_WE_PRE# = CPU_OWNS_SRAM ? Z80_WR#   : PICO_WE#
SRAM_OE_PRE# = CPU_OWNS_SRAM ? Z80_RD#   : PICO_OE#
SRAM_CE_PRE# = CPU_OWNS_SRAM ? Z80_MREQ# : PICO_CE#
```

| RESET# | BUSACK# | Selected source | Required state |
|----:|----:|----|----|
| 0 | X | Pico | Held-reset DMA or Z80 absent |
| 1 | 0 | Pico | BUSREQ#/BUSACK# DMA ownership |
| 1 | 1 | Z80 | Normal CPU execution |

This covers the two cases that BUSACK# alone cannot distinguish. During
RESET the Z80 drives BUSACK# inactive HIGH, and with the Z80 physically
absent its pull-up also reads HIGH. RESET# LOW therefore selects the Pico
in both cases. Only an out-of-reset CPU that has not granted the bus can
select MREQ#/RD#/WR#.

The PLD source adds the logically redundant consensus term
`Z80_CONTROL# & PICO_CONTROL#` to each output. It does not change this
truth table, but it holds an inactive HIGH continuously while RESET# or
BUSACK# transfers ownership with both candidate controls HIGH. Without
that term, unequal GAL product-term delays can create a static-1 hazard,
seen externally as a short active-low SRAM control pulse. Each hardened
equation uses four product terms, within every ATF22V10 macrocell's
capacity.

Program the selected ATF22V10B or ATF22V10C with
[src/pld/sram_control.pld](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/pld/sram_control.pld). Use the exact
device algorithm listed by the programmer, write the generated JEDEC
file, read it back, and require a verify pass before installation. Do
not enable the optional power-down mode or security fuse.

| ATF22V10 pin | Signal | Connection |
|----:|----|----|
| 1 | RESET# | Pico GP3 and Z80 pin 26 direct node |
| 2 | BUSACK# | Z80 pin 23 |
| 3 | PICO_WE# | Pico GP22 |
| 4 | Z80_WR# | Z80 pin 22 |
| 5 | PICO_OE# | Pico GP26 |
| 6 | Z80_RD# | Z80 pin 21 |
| 7 | PICO_CE# | Pico GP5 |
| 8 | Z80_MREQ# | Z80 pin 19 |
| 9 | DATA_ENABLE | Pico GP7; 10 kOhm pull-down to GND |
| 10 | ADDR_ENABLE | Pico GP9; 10 kOhm pull-down to GND |
| 11 | DATA_DIR | Pico GP6; 10 kOhm pull-down to GND |
| 12 | GND | Common ground |
| 13 | Z80_IORQ# | Z80 pin 20 direct 5 V node; existing 10 kOhm pull-up |
| 14 | SRAM_WE_PRE# | SN74AHCT244 2A2 pin 13 |
| 15 | SRAM_OE_PRE# | SN74AHCT244 2A3 pin 15 |
| 16 | SRAM_CE_PRE# | SN74AHCT244 2A4 pin 17 |
| 17 | DATA_UP_OE# | SN74AHCT245N OE# pin 19 |
| 18 | DATA_DOWN_OE# | SN74LVC245AN OE# pin 19 |
| 19 | MCP_RESET_DRIVE | 4.7 kOhm to Q1 base; Q1 collector drives MCP RESET# |
| 20 | WAIT# | Z80 pin 24 direct node; existing 10 kOhm pull-up |
| 21-23 | Unused outputs | Program constant LOW; leave open |
| 24 | VCC | Regulated +5 V |

The ATF22V10's guaranteed HIGH output is 2.4 V, which satisfies the
AHCT244's 2.0 V input threshold but not the AS6C1008's 5 V CMOS input
threshold. Never bypass the AHCT244 on these three paths. Fit 10 kOhm
pull-ups at AHCT244 2A2-2A4 and at the final SRAM CE#/OE#/WE# nodes. The
input pull-ups keep the permanently enabled AHCT244 deterministic if the
GAL is missing; the output pull-ups keep the SRAM inactive if the
AHCT244 is physically missing. All installed source devices must be
powered as required by the
[construction and power rules](inventory.md#04-construction-and-power).

WAIT# may connect directly from GAL pin 20 to Z80 pin 24 because the
Z84C00 ordinary-input HIGH minimum is 2.2 V, below the GAL's guaranteed
2.4 V HIGH. The existing 10 kOhm pull-up defines WAIT# HIGH if the GAL
is physically absent; it is not a substitute for powering an installed
GAL.

The same 2.4 V guaranteed HIGH exceeds the 2.0 V input-HIGH minimum of
both data-transceiver OE# inputs. Two additional equations use two
product terms each:

```text
DATA_UP_OE#    = NOT DATA_ENABLE OR NOT DATA_DIR
DATA_DOWN_OE#  = NOT DATA_ENABLE OR DATA_DIR
MCP_RESET_DRIVE = NOT ADDR_ENABLE
WAIT_N          = Z80_IORQ_N OR DATA_ENABLE
```

Both paths are disabled whenever DATA_ENABLE is LOW. When it is HIGH,
exactly one path is enabled. Firmware changes DATA_DIR only while
DATA_ENABLE is LOW, so the shared `NOT DATA_ENABLE` product term holds
both OE# outputs inactive throughout the transition. SN74LVC245A `Ioff`
permits its OE# pin to remain driven from the 5 V GAL while the LVC
device's 3.3 V supply is absent.

While IORQ# is inactive HIGH, WAIT# is always HIGH. When IORQ# falls,
WAIT# goes LOW because DATA_ENABLE is still LOW. After the trap configures
the correct direction and stable read/write data, raising DATA_ENABLE both
enables exactly one data path and releases WAIT#. Firmware keeps
DATA_ENABLE HIGH until IORQ# and RD#/WR# are inactive; lowering it early
would reassert WAIT# in the same cycle.

For address isolation, connect GAL pin 19 through 4.7 kOhm to the base
of Q1 (2N3904), add 47 kOhm base-to-emitter, ground the emitter, pull
MCP RESET# up to 5 V through 10 kOhm, and connect the collector to
RESET#. ADDR_ENABLE LOW makes GAL pin 19 HIGH and Q1 asserts reset,
which forces all MCP GPIO registers back to input mode. ADDR_ENABLE
HIGH releases reset; firmware then configures IODIR before using the
address bus.

During [Phase 6](../implementation/phase-6-sram.md) with the Z80 absent, hold
GP3 RESET# LOW. The programmed
logic then selects the Pico controls regardless of the pulled-up,
undriven BUSACK# input.

```mermaid
flowchart TB
  subgraph INPUTS["Control candidates"]
    direction LR
    PICO["Pico<br/>RESET#, WE#, OE#, CE#"]
    Z80["Z80<br/>BUSACK#, WR#, RD#, MREQ#"]
  end
  PICO --> GAL["ATF22V10<br/>three arbitration equations"]
  Z80 --> GAL
  GAL -->|"TTL-level pre-buffer controls"| HCT["SN74AHCT244<br/>channels 2A2-2A4"]
  HCT -->|"5 V CE#, OE#, WE#"| SRAM["AS6C1008 SRAM"]
```
