# PCB Inventory

This inventory is for **one PCB assembly**, not the three-breadboard
prototype. The [generated BOM](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/fabrication/z80_romless_sbc-bom.csv)
is authoritative for fitted references, values, and footprints. Sockets,
header strips, mounting hardware, and bench equipment are separate purchase
items.

## Semiconductors

| Reference | Quantity | Part | Package / requirement |
| --- | ---: | --- | --- |
| A1 | 1 | Raspberry Pi Pico 2 W | Two 20-pin headers; use a header-fitted module or solder and inspect the headers |
| U1 | 1 | Z84C0020PEC | PDIP-40, 15.24 mm row spacing; its 20 MHz grade is not a system qualification |
| U2 | 1 | AS6C1008-55PCN | PDIP-32, 15.24 mm row spacing; A16 is grounded for the lower 64 KiB |
| U3 | 1 | ATF22V10B-15PC or ATF22V10C-15PU | PDIP-24, 7.62 mm row spacing; requires compiled, programmed, and verified arbitration logic |
| U4 | 1 | SN74AHCT244N | PDIP-20, 7.62 mm row spacing; do not substitute an AHC part |
| U7 | 1 | SN74LVC244AN | PDIP-20, 7.62 mm row spacing; 3.3 V input-monitor buffer |
| U8 | 1 | MCP23S17-E/SP | SPDIP-28, 7.62 mm row spacing |
| U9 | 1 | SN74AHCT245N | PDIP-20, 7.62 mm row spacing; fixed Pico-to-5 V data path |
| U10 | 1 | SN74LVC245AN | PDIP-20, 7.62 mm row spacing; fixed 5 V-to-Pico data path |
| Q1 | 1 | 2N3904 | TO-92 inline; verify the purchased manufacturer's E/B/C lead order |
| D1 | 1 | 1N5819 | DO-41 axial, 10.16 mm lead pitch; banded cathode toward Pico VSYS |

Match the complete manufacturer part number and package. AHCT and LVC devices
are not interchangeable, even where their physical pin arrangements match.

## Resistors and networks

| References | Quantity | Value / form | Function |
| --- | ---: | --- | --- |
| R17-R22, R27, R28, R31 | 9 | 10 kOhm, 1/4 W axial | Remaining discrete startup bias |
| R23-R26, R29 | 5 | 4.7 kOhm, 1/4 W axial, 5% or better | Four GAL-input pull-downs and Q1 base current limiting |
| R30 | 1 | 47 kOhm, 1/4 W axial | Q1 base-emitter pull-down |
| RN1-RN2 | 2 | 8x10 kOhm bussed SIP, 9 pins | A0-A15 pull-ups; common pins to +5 V |
| RN3 | 1 | 8x10 kOhm bussed SIP, 9 pins | Pico D0-D7 pull-downs; common pin to GND |
| RN4-RN5 | 2 | 8x10 kOhm bussed SIP, 9 pins | Sixteen 5 V control pull-ups; common pins to +5 V |

Axial resistor footprints use a 6.3 mm body and 10.16 mm lead pitch. Buy
**bussed**, not isolated, SIP networks and verify their common-pin marking.
R23-R26 must remain 4.7 kOhm: the ATF22V10B's internal input pull-ups can
source enough current that 10 kOhm does not guarantee a valid startup LOW.

### Difference from the breadboard inventory

| Fitted part | Breadboard | PCB |
| --- | ---: | ---: |
| Discrete 10 kOhm resistors | 25 | 9 |
| Discrete 4.7 kOhm resistors | 5 | 5 |
| Discrete 47 kOhm resistors | 1 | 1 |
| 8x10 kOhm bussed SIP networks | 3 | 5 |

RN4/RN5 replace sixteen individual control pull-ups; they do not change the
required electrical bias. Do not add the sixteen breadboard resistors as
well as fitting these two PCB networks.

## Capacitors

| References | Quantity | Value / rating | Footprint |
| --- | ---: | --- | --- |
| C1-C8 | 8 | 100 nF ceramic, at least 10 V | 5 mm disc body, 2.50 mm lead pitch |
| C9-C11 | 3 | 22 uF polarized, at least 10 V | 8 mm radial body, 3.50 mm lead pitch |
| C12 | 1 | 100 uF polarized, at least 10 V | 10 mm radial body, 5.00 mm lead pitch |

The eight ceramics bypass individual IC supplies. C9-C11 provide distributed
5 V bulk capacitance; C12 is at the supply entry. C9-C12 positive pad 1 goes
to +5 V and negative pad 2 to GND. Retain their polarity silkscreen and check
the actual capacitor diameter and lead spacing before purchase.

## Connectors and test points

| Reference | Quantity | Item | Requirement |
| --- | ---: | --- | --- |
| J1 | 1 | Altech AK100 two-way screw terminal | 5.00 mm pitch; pad 1 is +5 V and pad 2 is GND |
| TP1-TP7 | 7 | Through-hole loop test points | 2.60 mm loop footprint, 0.90 mm drill; M1#, CLK, RESET#, WAIT#, BUSREQ#, BUSACK#, and GND |

## Sockets, headers, and mechanical items

| Quantity | Item | Requirement |
| ---: | --- | --- |
| 1 | Four-layer PCB | 160 x 135 mm, 1.6 mm FR-4, internal Pico antenna cutout |
| 1 | 40-pin DIP socket | Wide 15.24 mm row spacing for U1 |
| 1 | 32-pin DIP socket | Wide 15.24 mm row spacing for U2 |
| 1 | 28-pin DIP socket | 7.62 mm row spacing for U8 |
| 1 | 24-pin DIP socket | 7.62 mm row spacing for U3 |
| 4 | 20-pin DIP sockets | 7.62 mm row spacing for U4/U7/U9/U10 |
| 2 | 20-position socket strips | 2.54 mm pitch, for a removable Pico; not DIP IC sockets |
| 2, if not already fitted | 20-pin male header strips | 2.54 mm pitch, soldered to the Pico |
| 4 | M3 mounting fastener/standoff sets | Match the four 3.2 mm non-plated mounting holes |

Socket overhang and component heights are not established by the package
name or an incomplete 3D rendering. Check the purchased dimensions and
extraction access, especially between U1 and U4.

## Power, programming, and bench equipment

Use a regulated 5 V supply rated for at least 1 A, preferably with adjustable
current limiting. The main supply powers the Pico through D1 into VSYS;
USB is optional for operation. Follow the
[power-sequencing restrictions](design-considerations.md#power-and-startup-safety)
when attaching USB for programming or diagnostics.

Provide a suitable GAL programmer and fitter, a USB data cable, a multimeter,
and the shared [oscilloscope](../hardware/oscilloscope.md) and
[logic-analyzer](../hardware/logic-analyzer.md) equipment. Bench equipment
and temporary diagnostic resistors are not fitted PCB BOM entries.
