# 0. Project Inventory

This is the **eight-active-package assisted-loader design**, excluding the
power diode. No PLD, PLD programmer,
address expander, or reset transistor is needed. Match the complete part
number and PDIP package before ordering.

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

## Bill of Materials — Pricing and Suppliers

Prices checked on **24 October 2026**, in **USD** from **digikey.com** except
the user-supplied AliExpress Z80 price below,
excluding shipping, tax, and any tariffs. These are the displayed catalogue
prices, not a quotation; recheck price and stock before ordering. Unit prices
use the quantity-one tier except the 39 resistors, which use the 25-piece tier.
Line totals are rounded to cents. Links identify the priced ordering option.

The Z80 listing costs **AU$9.29 for two**, as supplied by the project owner.
Using [1 AUD = 0.70156 USD on 24 October 2026](https://exchangeratetool.com/historical/aud-usd),
that is approximately **US$6.52 per two-pack**, or **US$3.26 per CPU**.
The BOM allocates one CPU to this build; purchasing the pack leaves one spare.
Select the specified Z84C0020PEC DIP-40 variant. The AliExpress price was
provided by the project owner and could not be independently checked.

Sockets and Pico headers are priced separately below, although the inventory
checks above group them with their devices. Passive components and connectors
without a specified manufacturer use the listed through-hole ordering examples.
The Pico needs **two separate 1x20 male headers**, one along each side, plus
two matching socket strips.

### Base circuit and breadboard construction

| Qty | Component / use | Supplier / ordering option | Unit (USD) | Line total (USD) |
| ---: | --- | --- | ---: | ---: |
| 1 | A1 Raspberry Pi Pico 2 W, without headers | [SC1633 — 2648-SC1633CT-ND](https://www.digikey.com/en/products/detail/raspberry-pi/SC1633/25862726) | $7.00 | $7.00 |
| 1 kit | A1 pair of 1x20 male headers | [SparkFun 17907 — 1568-17907-ND](https://www.digikey.com/en/products/detail/sparkfun-electronics/17907/13910058) | $1.59 | $1.59 |
| 2 | A1 1x20, 2.54 mm socket strips | [Würth 61302011821 — 732-61302011821-ND](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/61302011821/16608603) | $0.88 | $1.76 |
| 1 | U1 Z84C0020PEC, DIP-40 | [AliExpress two-pack](https://www.aliexpress.com/item/1005006323138580.html); not stocked by DigiKey | ~$3.26 | ~$3.26 |
| 1 | U2 AS6C1008-55PCN, DIP-32 | [1450-1017-ND](https://www.digikey.com/en/products/detail/alliance-memory-inc/AS6C1008-55PCN/4234576) | $9.37 | $9.37 |
| 1 | U3 SN74HCT32N, DIP-14 | [296-1615-5-ND](https://www.digikey.com/en/products/detail/texas-instruments/SN74HCT32N/277261) | $0.99 | $0.99 |
| 1 | U4 SN74AHCT244N, DIP-20 | [296-4723-5-ND](https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT244N/375869) | $1.24 | $1.24 |
| 1 | U7 SN74LVC244AN, DIP-20 | [296-8501-5-ND](https://www.digikey.com/en/products/detail/texas-instruments/SN74LVC244AN/377479) | $1.18 | $1.18 |
| 1 | U9 SN74AHCT245N, DIP-20 | [296-1476-5-ND](https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT245N/277122) | $1.12 | $1.12 |
| 1 | U10 SN74LVC245AN, DIP-20 | [296-8503-5-ND](https://www.digikey.com/en/products/detail/texas-instruments/SN74LVC245AN/377483) | $1.83 | $1.83 |
| 1 | U1 DIP-40 socket, 15.24 mm row spacing | [On Shore ED40DT — ED3048-5-ND](https://www.digikey.com/en/products/detail/on-shore-technology-inc/ED40DT/4147604) | $2.95 | $2.95 |
| 1 | U2 DIP-32 socket, 15.24 mm row spacing | [On Shore ED32DT — ED3053-5-ND](https://www.digikey.com/en/products/detail/on-shore-technology-inc/ED32DT/4147603) | $0.97 | $0.97 |
| 1 | U3 DIP-14 socket, 7.62 mm row spacing | [On Shore ED14DT — ED3045-5-ND](https://www.digikey.com/en/products/detail/on-shore-technology-inc/ED14DT/4147595) | $0.52 | $0.52 |
| 4 | U4/U7/U9/U10 DIP-20 sockets, 7.62 mm row spacing | [On Shore ED20DT — ED3054-5-ND](https://www.digikey.com/en/products/detail/on-shore-technology-inc/ED20DT/4147598) | $1.85 | $7.40 |
| 1 | D1 1N5819 Schottky diode, DO-41 | [Diodes Inc. 1N5819-T — 1N5819DICT-ND](https://www.digikey.com/en/products/detail/diodes-incorporated/1N5819-T/190960) | $0.59 | $0.59 |
| 7 | 100 nF ceramic, 50 V, X7R, radial | [Vishay K104K15X7RF5TL2 — BC1084CT-ND](https://www.digikey.com/en/products/detail/vishay-beyschlag-draloric-bc-components/K104K15X7RF5TL2/286706) | $0.31 | $2.17 |
| 3 | 22 uF electrolytic, 25 V, radial | [Panasonic ECA-1EM220 — P5149-ND](https://www.digikey.com/en/products/detail/panasonic-industry/ECA-1EM220/245008) | $0.23 | $0.69 |
| 1 | 100 uF electrolytic, 25 V, radial | [Panasonic ECA-1EM101B — 10-ECA-1EM101BCT-ND](https://www.digikey.com/en/products/detail/panasonic-industry/ECA-1EM101B/268461) | $0.33 | $0.33 |
| 39 | Permanent 10 kOhm resistors, 1/4 W, 5%, axial | [YAGEO CFR-25JB-52-10K](https://www.digikey.com/en/products/detail/yageo/CFR-25JB-52-10K/338) | $0.03040 | $1.19 |
| 3 | BB830 830-point breadboards | [4526-BB830-ND](https://www.digikey.com/en/products/detail/busboard-prototype-systems/BB830/19200392) | $19.73 | $59.19 |
| 1 kit | 22 AWG solid-core wire, six colours, 25 ft each | [Adafruit 1311 — 1528-1743-ND](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1311/6198255) | $15.95 | $15.95 |
| 1 | USB-A to Micro-B data cable, 1 m | [Cvilux DH-20M50055 — 2987-DH-20M50055-ND](https://www.digikey.com/en/products/detail/cvilux-usa/DH-20M50055/13175849) | $1.58 | $1.58 |

**Base subtotal: approximately $122.87 USD, including one Z80.** Buying the
full two-pack brings the purchase subtotal to **approximately $126.13 USD**,
including a spare CPU. The DigiKey-only subtotal remains **$119.61 USD**.
This includes the full wire kit, three breadboards, sockets, headers, and USB
cable. The subtotals exclude bench equipment, temporary test parts, spares,
optional J2, shipping, tax, and tariffs.

## PCB Status

The revised schematic also includes J2, an optional 1x5, 2.54 mm expansion
header for GP8, GP22, GP26, 3.3 V, and GND. Reserve one five-pin header for
the future PCB; it adds no active packages and is not required for the
Phase 0-10 breadboard build. No peripheral pull-ups or LED resistors are
included. See the [expansion pin map](pin-mapping.md#j2-peripheral-expansion).

The existing PCB BOM, routing, and fabrication package are for the previous
design and **must not be used to manufacture this circuit**. PCB updates are
deferred. Use the revised native schematic and these breadboard instructions.
