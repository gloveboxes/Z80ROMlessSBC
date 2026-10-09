# 0. Project Inventory

This is the **eight-active-package assisted-loader design**, excluding the
power diode. No PLD, PLD programmer, address expander, or reset transistor is
needed. Match the complete part number and PDIP package before ordering.

## Semiconductors and Sockets

| Qty | Reference | Part | Package / socket | Role |
| ---: | --- | --- | --- | --- |
| 1 | A1 | Raspberry Pi Pico 2 W | Soldered 2x20 headers; two socket strips | Supervisor, injected loading, USB, Wi-Fi, flash disks |
| 1 | U1 | Z84C0020PEC | DIP-40, 15.24 mm wide socket | Fully static CMOS Z80 |
| 1 | U2 | AS6C1008-55PCN | DIP-32, 15.24 mm wide socket | Lower 64 KiB SRAM |
| 1 | U3 | SN74HCT32N | DIP-14, 7.62 mm socket | Three fixed OR gates |
| 1 | U4 | SN74AHCT244N | DIP-20, 7.62 mm socket | Clock and SRAM-control translation |
| 1 | U7 | SN74LVC244AN | DIP-20, 7.62 mm socket | Eight 5 V-tolerant monitor inputs |
| 1 | U9 | SN74AHCT245N | DIP-20, 7.62 mm socket | Pico-to-Z80 data |
| 1 | U10 | SN74LVC245AN | DIP-20, 7.62 mm socket | Z80-to-Pico data |
| 1 | D1 | 1N5819 | DO-41 axial | External +5 V to Pico VSYS |

Do not interchange AHCT, AHC, HCT, HC, or LVC parts. Pico headers must be
soldered, not merely pushed through unsoldered holes. U5/U6/U8 are not fitted;
reference gaps retain continuity with existing project labels.

## Passive Components

| Qty | Item | Purpose |
| ---: | --- | --- |
| 7 | 100 nF ceramic, at least 10 V | One per DIP IC |
| 3 | 22 uF, at least 10 V | One +5 V bulk capacitor per board |
| 1 | 100 uF, at least 10 V | Supply-entry bulk |
| 4 | Bussed SIP-9, 8x10 kOhm | RN1/RN2 address pulls, RN3 Pico data pulls, RN4 CPU control pulls |
| 7 | 10 kOhm discrete | R17-R23 startup controls |
| Test only | 1 kOhm discrete | Manual input and first-drive current limiting |

The four networks contain 32 independent resistor branches. For discrete
breadboard construction use **39 individual 10 kOhm resistors instead of the
four networks plus seven discrete resistors**, not both alternatives. No
4.7 kOhm or 47 kOhm reset-transistor parts remain.

## Construction and Equipment

Use three BB830 830-point breadboards, short 22 AWG solid-core wire, a
regulated +5 V supply rated at least 1 A with adjustable current limiting,
a USB data cable, and an emergency supply disconnect. Have a multimeter,
[DHO814 scope](oscilloscope.md), and [DSLogic Plus](logic-analyzer.md) available.
Keep spare capacitors, wire, and an IC extractor handy.

External +5 V must precede USB; remove USB before external +5 V. Fitted
AHCT245 data ports are not power-off-safe. See the [power phase](../implementation/phase-0-power.md).

## PCB Status

The existing PCB BOM, routing, and fabrication package are for the previous
design and **must not be used to manufacture this circuit**. PCB updates are
deferred. Use the revised native schematic and these breadboard instructions.