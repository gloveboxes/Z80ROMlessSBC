# 0. Project Inventory

This is the **eight-active-package assisted-loader design**, excluding the
power diode. No PLD, PLD programmer, address expander, or reset transistor is
needed. Match the complete part number and PDIP package before ordering.

<div data-checklist="hardware-inventory" data-checklist-label="hardware inventory" markdown="1">

Tick an item when you have the **full listed quantity** available, including
the specified socket or headers where applicable. These are inventory checks,
not installation or electrical-test results. Progress is saved in this browser
on this device, separately from the implementation-phase checklists.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

## Semiconductors and Sockets

| Done | Qty | Reference | Part | Package / socket | Role |
| --- | ---: | --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="a1" aria-label="Pico 2 W headers and socket strips available"> | 1 | A1 | Raspberry Pi Pico 2 W | Soldered 2x20 headers; two socket strips | Supervisor, injected loading, USB, Wi-Fi, flash disks |
| <input type="checkbox" data-checklist-id="u1" aria-label="Z80 and DIP-40 socket available"> | 1 | U1 | Z84C0020PEC | DIP-40, 15.24 mm wide socket | Fully static CMOS Z80 |
| <input type="checkbox" data-checklist-id="u2" aria-label="SRAM and DIP-32 socket available"> | 1 | U2 | AS6C1008-55PCN | DIP-32, 15.24 mm wide socket | Lower 64 KiB SRAM |
| <input type="checkbox" data-checklist-id="u3" aria-label="HCT32 and DIP-14 socket available"> | 1 | U3 | SN74HCT32N | DIP-14, 7.62 mm socket | Three fixed OR gates |
| <input type="checkbox" data-checklist-id="u4" aria-label="AHCT244 and DIP-20 socket available"> | 1 | U4 | SN74AHCT244N | DIP-20, 7.62 mm socket | Clock and SRAM-control translation |
| <input type="checkbox" data-checklist-id="u7" aria-label="LVC244 and DIP-20 socket available"> | 1 | U7 | SN74LVC244AN | DIP-20, 7.62 mm socket | Eight 5 V-tolerant monitor inputs |
| <input type="checkbox" data-checklist-id="u9" aria-label="AHCT245 and DIP-20 socket available"> | 1 | U9 | SN74AHCT245N | DIP-20, 7.62 mm socket | Pico-to-Z80 data |
| <input type="checkbox" data-checklist-id="u10" aria-label="LVC245 and DIP-20 socket available"> | 1 | U10 | SN74LVC245AN | DIP-20, 7.62 mm socket | Z80-to-Pico data |
| <input type="checkbox" data-checklist-id="d1" aria-label="1N5819 diode available"> | 1 | D1 | 1N5819 | DO-41 axial | External +5 V to Pico VSYS |

Do not interchange AHCT, AHC, HCT, HC, or LVC parts. Pico headers must be
soldered, not merely pushed through unsoldered holes. U5/U6/U8 are not fitted;
reference gaps retain continuity with existing project labels.

## Passive Components

| Done | Qty | Item | Purpose |
| --- | ---: | --- | --- |
| <input type="checkbox" data-checklist-id="ceramic-100nf" aria-label="Seven 100 nF ceramic capacitors available"> | 7 | 100 nF ceramic, at least 10 V | One per DIP IC |
| <input type="checkbox" data-checklist-id="bulk-22uf" aria-label="Three 22 uF bulk capacitors available"> | 3 | 22 uF, at least 10 V | One +5 V bulk capacitor per board |
| <input type="checkbox" data-checklist-id="bulk-100uf" aria-label="One 100 uF supply-entry capacitor available"> | 1 | 100 uF, at least 10 V | Supply-entry bulk |
| <input type="checkbox" data-checklist-id="resistors-10k" aria-label="All 39 permanent 10 kOhm resistors available"> | 39 | Individual 10 kOhm resistors | 16 address pull-ups, eight Pico data pull-downs, eight CPU control pull-ups, seven startup pulls |
| <input type="checkbox" data-checklist-id="test-resistors-1k" aria-label="Temporary 1 kOhm test resistors available"> | Test only | 1 kOhm discrete | Manual input and first-drive current limiting |

### Permanent breadboard resistors

**Use 39 individual (discrete) 10 kOhm resistors. No resistor-network packages
are fitted on the breadboard.** Each resistor connects one signal to its
specified supply rail or GND; signal wires must not be joined together.
A resistor to a supply is a **pull-up**, and a resistor to GND is a
**pull-down**. These establish defaults when no active output drives a signal.

The group checks below divide the **same 39 resistors** listed above; they
are not additional parts. Tick each group when its quantity is set aside.

| Done | Quantity | Signals | Other end of each resistor |
| --- | ---: | --- | --- |
| <input type="checkbox" data-checklist-id="address-pulls" aria-label="Sixteen address pull-up resistors set aside"> | 16 | Address A0-A15, one resistor per bit | +5 V |
| <input type="checkbox" data-checklist-id="data-pulls" aria-label="Eight Pico data pull-down resistors set aside"> | 8 | Pico data GP10-GP17, one resistor per bit | GND |
| <input type="checkbox" data-checklist-id="control-pulls" aria-label="Eight CPU control pull-up resistors set aside"> | 8 | BUSREQ#, BUSACK#, MREQ#, IORQ#, RD#, WR#, INT#, NMI# | +5 V |
| <input type="checkbox" data-checklist-id="startup-up-pulls" aria-label="Four Pico startup pull-up resistors set aside"> | 4 | Pico GP4, GP5, GP6, GP7 startup controls | +3.3 V |
| <input type="checkbox" data-checklist-id="startup-down-pulls" aria-label="Three Pico startup pull-down resistors set aside"> | 3 | Pico GP2, GP3, GP9 startup controls | GND |
| | **39** | **Total permanent resistors** | |

Install all permanent resistors in Phase 0, following the
[installation table](../implementation/phase-0-power.md#passive-component-installation).
This is before fitting any active devices.

Temporary test resistors are additional bench items, not part of the 39
permanent branches. Later phases specify when to fit and remove 1 kOhm test
resistors and extra 10 kOhm data-pattern pulls. Keep spares for those tests.
No 4.7 kOhm or 47 kOhm reset-transistor parts remain.

## Construction and Equipment

| Done | Qty | Item | Requirement / purpose |
| --- | ---: | --- | --- |
| <input type="checkbox" data-checklist-id="breadboards" aria-label="Three BB830 breadboards available"> | 3 | BB830 breadboards | 830-point boards |
| <input type="checkbox" data-checklist-id="wire" aria-label="22 AWG solid-core hookup wire available"> | As needed | Hookup wire | Short 22 AWG solid-core connections |
| <input type="checkbox" data-checklist-id="power-supply" aria-label="Regulated 5 V current-limited supply available"> | 1 | Regulated +5 V supply | Rated at least 1 A, with adjustable current limiting |
| <input type="checkbox" data-checklist-id="usb-cable" aria-label="USB data cable available"> | 1 | USB data cable | Pico programming and diagnostics; not a charge-only cable |
| <input type="checkbox" data-checklist-id="supply-disconnect" aria-label="Emergency supply disconnect available"> | 1 | Emergency supply disconnect | Accessible means to disconnect external power |
| <input type="checkbox" data-checklist-id="multimeter" aria-label="Multimeter available"> | 1 | Multimeter | Continuity, resistance and rail-voltage checks |
| <input type="checkbox" data-checklist-id="oscilloscope" aria-label="DHO814 oscilloscope available"> | 1 | [DHO814 scope](oscilloscope.md) | Analog signal and timing measurements |
| <input type="checkbox" data-checklist-id="logic-analyzer" aria-label="DSLogic Plus logic analyzer available"> | 1 | [DSLogic Plus](logic-analyzer.md) | Digital bus captures |
| <input type="checkbox" data-checklist-id="spares" aria-label="Spare capacitors wire and test pull resistors available"> | As needed | Spare capacitors, wire and 10 kOhm test resistors | Replacement parts and temporary data-pattern pulls |
| <input type="checkbox" data-checklist-id="ic-extractor" aria-label="IC extractor available"> | 1 | IC extractor | Socketed device removal |

External +5 V must precede USB; remove USB before external +5 V. Fitted
AHCT245 data ports are not power-off-safe. See the [power phase](../implementation/phase-0-power.md).

</div>

## PCB Status

The existing PCB BOM, routing, and fabrication package are for the previous
design and **must not be used to manufacture this circuit**. PCB updates are
deferred. Use the revised native schematic and these breadboard instructions.