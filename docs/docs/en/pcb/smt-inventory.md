# Partial-SMT PCB Inventory and Assembly

These parts are for `hardware/kicad-smt/`, not the
[DIP / through-hole inventory](inventory.md). The
[full SMT BOM](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad-smt/fabrication/z80_romless_sbc_smt-bom.csv)
owns fitted references, values and footprints. The
[JLC assembly BOM](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad-smt/fabrication/assembly/z80_romless_sbc_smt-jlc-bom.csv)
contains only factory-assembled parts.

## JLC-assembled semiconductors and power connector

| Ref | Exact part | Package / mounting | LCSC |
| --- | --- | --- | --- |
| A1 | Raspberry Pi Pico 2 W | Reflowed castellated module, `RaspberryPi_Pico_W_SMD` | C42394205 |
| U2 | IS62C1024AL-35QLI | SOP-32, 450 mil, 1.27 mm pitch; 128K x 8, 35 ns | C397875 |
| U3 | SN74HCT32DR | SOIC-14, 3.9 mm body | C6781 |
| U4 | SN74AHCT244DWR | Wide SOIC-20, 7.5 mm body | C2675568 |
| U7 | SN74LVC244ADWR | Wide SOIC-20, 7.5 mm body | C507250 |
| U9 | SN74AHCT245DWR | Wide SOIC-20, 7.5 mm body | C94773 |
| U10 | SN74LVC245ADWR | Wide SOIC-20, 7.5 mm body | C571201 |
| D1 | SS14 | SMA Schottky; cathode toward Pico VSYS | C2480 |
| J1 | TYPE-C 16PIN power receptacle | HRO-pattern USB-C footprint; power only | C393939 |

Do not substitute HCT, AHCT or LVC families, narrow SOIC packages, or the
TSOP SRAM variant. Verify stock and the exact part against the uploaded
BOM. No Pico socket strips are fitted to this design.

## Resistors

| References | Qty | Value / package | LCSC |
| --- | ---: | --- | --- |
| R1-R40 | 40 | 10k, 1%, 0603 | C25804 |
| R41, R42 | 2 | 5.1k, 1%, 0603 | C23186 |

The 40 startup/bus pulls have the same electrical roles as the breadboard
and DIP board, but use individual SMT resistors rather than bussed arrays:

| References | Role | Pull rail |
| --- | --- | --- |
| R1-R16 | A0-A15 address pull-ups | +5V |
| R17-R24 | Pico D0-D7 pull-downs | GND |
| R25-R32 | BUSREQ#, BUSACK#, MREQ#, IORQ#, RD#, WR#, INT#, NMI# pull-ups | +5V |
| R33-R36 | GP4/GP5/GP6/GP7 startup pull-ups | +3V3 |
| R37-R40 | GP2/GP3/GP9/GP8 startup pull-downs | GND |
| R41, R42 | Separate USB-C CC1/CC2 sink pull-downs | GND |

SMT references R17-R24 are data pulls, **not** the breadboard's R17-R24
startup resistors. Match by signal and rail, not by resistor reference.
The CC resistors are additional to the 40 bus/startup pulls.

## Capacitors

| References | Qty | Value / rating | Package | LCSC |
| --- | ---: | --- | --- | --- |
| C1-C7 | 7 | 100 nF, 50 V, X7R | 0603 | C14663 |
| C8-C10 | 3 | 22 uF, 35 V | SMT electrolytic, 6.3 x 5.4 mm footprint | C150379 |
| C11 | 1 | 100 uF, 16 V | SMT electrolytic, 6.3 x 5.4 mm footprint | C346929 |

C1-C4/C6 bypass the +5V devices; C5/C7 bypass the +3V3 LVC devices.
C8-C11 positive pad 1 is +5V and negative pad 2 is GND.
The 22 uF and 100 uF parts look the same size in JLC's preview; check their
part numbers and values, not rendered body size.

## Four hand-fitted items

| Ref | Part to fit after PCBA | Requirement |
| --- | --- | --- |
| U1 | Z84C0020PEC Z80 | DIP-40, 15.24 mm pin-row spacing; hand-soldered directly to this board |
| A2 | 0.91-inch SSD1315 128x32 OLED and female socket | 38 x 12 mm module; 1x4, 2.54 mm socket, 5.0 mm body; 3.3 V |
| J2 | GPIO expansion header | 1x4, 2.54 mm: GP22/SDA, GP26/SCL, 3.3 V, GND |
| J3 | Auxiliary power/probe header | 1x2, 2.54 mm: +5V, GND; use one source, J1 or J3 |

The OLED socket order is **GND, VCC, SCK/SCL, SDA**, bottom-to-top viewed
from above. Verify socket-tail fit in the 1.0 mm holes and the module's
clearance above the low-profile SMT parts. Do not assume the layout
reserves space for a Z80 socket; qualify any change to the specified
direct-solder assembly separately.

## Mechanical hardware and equipment

- One four-layer, 1.6 mm FR-4 PCB, **98.3 x 60 mm**, with its enclosed
  antenna cutout.
- Four M2.5 standoffs/fasteners for the 2.7 mm NPTH holes. Hole centres
  are 3.2 mm from adjacent edges, at **91.9 x 53.6 mm** spacing.
- An insulating OLED support if required by the purchased socket/module.
- A regulated, preferably current-limited +5V source and the shared
  [bench instruments](../hardware/index.md).

Apply external +5V before connecting Pico USB; disconnect Pico USB first.
Do not power the populated board from Pico USB alone or connect both
J1 and J3 supplies. See [power and startup safety](smt-design-considerations.md#power-and-startup-safety).
