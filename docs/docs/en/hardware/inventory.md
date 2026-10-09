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
| 39 | Individual 10 kOhm resistors | 16 address pull-ups, eight Pico data pull-downs, eight CPU control pull-ups, seven startup pulls |
| Test only | 1 kOhm discrete | Manual input and first-drive current limiting |

### Permanent breadboard resistors

**Use 39 individual (discrete) 10 kOhm resistors. No resistor-network packages
are fitted on the breadboard.** Each resistor connects one signal to its
specified supply rail or GND; signal wires must not be joined together.
A resistor to a supply is a **pull-up**, and a resistor to GND is a
**pull-down**. These establish defaults when no active output drives a signal.

| Quantity | Signals | Other end of each resistor |
| ---: | --- | --- |
| 16 | Address A0-A15, one resistor per bit | +5 V |
| 8 | Pico data GP10-GP17, one resistor per bit | GND |
| 8 | BUSREQ#, BUSACK#, MREQ#, IORQ#, RD#, WR#, INT#, NMI# | +5 V |
| 4 | Pico GP4, GP5, GP6, GP7 startup controls | +3.3 V |
| 3 | Pico GP2, GP3, GP9 startup controls | GND |
| **39** | **Total permanent resistors** | |

Install all permanent resistors in Phase 0, following the
[installation table](../implementation/phase-0-power.md#passive-component-installation).
This is before fitting any active devices.

Temporary test resistors are additional bench items, not part of the 39
permanent branches. Later phases specify when to fit and remove 1 kOhm test
resistors and extra 10 kOhm data-pattern pulls. Keep spares for those tests.
No 4.7 kOhm or 47 kOhm reset-transistor parts remain.

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