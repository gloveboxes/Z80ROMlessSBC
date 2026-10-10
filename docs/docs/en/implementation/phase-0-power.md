# Phase 0 - Empty Sockets and Power Distribution

**Install:** Three boards, empty sockets, all passive defaults and supply
wiring. No active devices, including the Pico. Use the [inventory](../hardware/inventory.md)
and [orientation table](../hardware/construction.md#31-package-orientation-and-pin-1).
This phase has no firmware target.

## Wiring - Sockets and Rails

Feed three separate +5 V rails from one regulated supply-entry star and bond
all grounds with short links. Confirm rail segments are continuous with a
meter. Keep a separate Peripheral 3.3 V rail for Pico header 36, U7 and U10;
leave any supply-module 3.3 V output disconnected.

Wire supply contacts from the [fixed-pin table](../hardware/pin-mapping.md#power-and-fixed-pins).
External +5 V goes to the 1N5819 anode; its banded cathode goes to VSYS header
39. Leave VBUS header 40 open. Do not bypass the diode.

## Passive-Component Installation

**Keep external power and USB disconnected throughout installation. All IC
sockets and the Pico socket strips remain empty.** Fit 39 individual 10 kOhm
resistors and 11 capacitors using the steps below.

Tick the **Done** box beside each component as you fit it. With JavaScript
enabled, progress is saved in this browser on this device and restored when
you return. It does not sync across devices; clearing site data removes it.
Use **Clear checklist** to start again. Ticks record installation only, not
electrical verification or a passed test gate.

<button type="button" class="md-button" id="clear-phase-0-checklist" hidden>Clear checklist</button>
<p id="phase-0-checklist-status" role="status" aria-live="polite"></p>

### How to use these connection instructions

- A **socket pin** means the contact where that IC pin will go when the chip
  is fitted later. A **Pico header pin** is its physical header position,
  not its GP number.
- Put a component lead into a free hole electrically connected to the named
  socket contact. Do not force two leads into one hole. If necessary, use a
  short jumper from that contact to a separate terminal strip.
- On a BB830, A-E in one numbered row are connected together, and F-J in
  that row are a separate connected group. Adjacent rows are not connected.
  Both leads in the same connected group would short out the component.
- Use the [orientation table](../hardware/construction.md#31-package-orientation-and-pin-1)
  to identify socket pins. Do not guess universal hole coordinates: chip
  rotations differ, and the wide Z80/SRAM sockets do not straddle E/F like
  the narrow logic ICs.
- Check each endpoint at the actual socket contact with a meter. Signal
  jumpers to other devices are added in later phases; the passive can be
  attached to its named socket now.

### Step 1 - Fit seven local 100 nF ceramic capacitors

A local bypass capacitor supplies short bursts of current at an IC. Fit one
beside each DIP socket's supply pin, with short connections to that supply
contact and nearby common GND. These ceramic capacitors are **not polarized**:
either lead can face GND. Use capacitors rated at least 10 V.

| Done | Board | Capacitor | Socket / device | One lead connects to | Other lead connects to |
| --- | --- | --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="c1" aria-label="C1 installed"> | Core | C1, 100 nF | U1 Z80 | VCC socket pin 11, +5 V | Common GND, also connected to socket pin 29 |
| <input type="checkbox" data-checklist-id="c2" aria-label="C2 installed"> | Memory | C2, 100 nF | U2 SRAM | VCC socket pin 32, +5 V | Common GND, also connected to socket pin 16 |
| <input type="checkbox" data-checklist-id="c3" aria-label="C3 installed"> | Memory | C3, 100 nF | U3 HCT32 | VCC socket pin 14, +5 V | Common GND, also connected to socket pin 7 |
| <input type="checkbox" data-checklist-id="c4" aria-label="C4 installed"> | Core | C4, 100 nF | U4 AHCT244 | VCC socket pin 20, +5 V | Common GND, also connected to socket pin 10 |
| <input type="checkbox" data-checklist-id="c5" aria-label="C5 installed"> | Peripheral | C5, 100 nF | U7 LVC244 | VCC socket pin 20, **+3.3 V** | Common GND, also connected to socket pin 10 |
| <input type="checkbox" data-checklist-id="c6" aria-label="C6 installed"> | Peripheral | C6, 100 nF | U9 AHCT245 | VCC socket pin 20, +5 V | Common GND, also connected to socket pin 10 |
| <input type="checkbox" data-checklist-id="c7" aria-label="C7 installed"> | Peripheral | C7, 100 nF | U10 LVC245 | VCC socket pin 20, **+3.3 V** | Common GND, also connected to socket pin 10 |

The GND pin numbers identify the electrical connection; do not stretch the
capacitor across the whole package to reach a distant GND pin. Use nearby GND
with a short return path. The Pico module already has onboard decoupling;
it is not an eighth DIP capacitor in this list.

### Step 2 - Fit four bulk capacitors

Bulk capacitors support the supply rails. These are **polarized**: connect
the positive lead to +5 V and the negative lead to GND. Identify polarity
from the case markings, not just lead length. Use capacitors rated at least
10 V.

| Done | Capacitor | Where to place it | Positive lead | Negative lead |
| --- | --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="c8" aria-label="C8 installed"> | C8, 22 uF | Memory board, beside its +5 V supply feed | Memory +5 V rail | Memory GND rail |
| <input type="checkbox" data-checklist-id="c9" aria-label="C9 installed"> | C9, 22 uF | Core board, beside its +5 V supply feed | Core +5 V rail | Core GND rail |
| <input type="checkbox" data-checklist-id="c10" aria-label="C10 installed"> | C10, 22 uF | Peripheral board, beside its +5 V supply feed | Peripheral **+5 V** rail, not +3.3 V | Peripheral GND rail |
| <input type="checkbox" data-checklist-id="c11" aria-label="C11 installed"> | C11, 100 uF | Common supply entry, before the feed fans out to the three boards | External +5 V entry | Common GND entry |

All four go **across** their supply and GND, not in series with the supply
wire. They supplement the seven local ceramic capacitors, not replace them.
The current parts list specifies no separate 3.3 V bulk capacitor; retain
the local 100 nF capacitors at both LVC sockets.

### Step 3 - Fit sixteen SRAM address pull-ups on Memory

For **each row below**, fit one individual 10 kOhm resistor. Connect one end
to the Memory +5 V rail and the other end to the listed U2 SRAM socket pin.
The resistor can face either way. Leave SRAM absent.

| Done | Signal | SRAM socket pin | Resistor's other end |
| --- | --- | ---: | --- |
| <input type="checkbox" data-checklist-id="sram-a14" aria-label="SRAM A14 pull-up installed"> | A14 | 3 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a12" aria-label="SRAM A12 pull-up installed"> | A12 | 4 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a7" aria-label="SRAM A7 pull-up installed"> | A7 | 5 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a6" aria-label="SRAM A6 pull-up installed"> | A6 | 6 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a5" aria-label="SRAM A5 pull-up installed"> | A5 | 7 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a4" aria-label="SRAM A4 pull-up installed"> | A4 | 8 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a3" aria-label="SRAM A3 pull-up installed"> | A3 | 9 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a2" aria-label="SRAM A2 pull-up installed"> | A2 | 10 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a1" aria-label="SRAM A1 pull-up installed"> | A1 | 11 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a0" aria-label="SRAM A0 pull-up installed"> | A0 | 12 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a10" aria-label="SRAM A10 pull-up installed"> | A10 | 23 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a11" aria-label="SRAM A11 pull-up installed"> | A11 | 25 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a9" aria-label="SRAM A9 pull-up installed"> | A9 | 26 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a8" aria-label="SRAM A8 pull-up installed"> | A8 | 27 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a13" aria-label="SRAM A13 pull-up installed"> | A13 | 28 | +5 V |
| <input type="checkbox" data-checklist-id="sram-a15" aria-label="SRAM A15 pull-up installed"> | A15 | 31 | +5 V |

Do not join the address pins together. **A16, socket pin 2, connects directly
to GND**, not through a pull-up; it selects the lower 64 KiB.

### Step 4 - Fit eight Z80 control pull-ups on Core

For **each row below**, fit one individual 10 kOhm resistor from the Core
+5 V rail to the listed U1 Z80 socket contact. These define controls while
the CPU is absent or its bus outputs are floating.

| Done | Signal | Z80 socket pin | Resistor's other end |
| --- | --- | ---: | --- |
| <input type="checkbox" data-checklist-id="z80-int" aria-label="Z80 INT# pull-up installed"> | INT# | 16 | +5 V |
| <input type="checkbox" data-checklist-id="z80-nmi" aria-label="Z80 NMI# pull-up installed"> | NMI# | 17 | +5 V |
| <input type="checkbox" data-checklist-id="z80-mreq" aria-label="Z80 MREQ# pull-up installed"> | MREQ# | 19 | +5 V |
| <input type="checkbox" data-checklist-id="z80-iorq" aria-label="Z80 IORQ# pull-up installed"> | IORQ# | 20 | +5 V |
| <input type="checkbox" data-checklist-id="z80-rd" aria-label="Z80 RD# pull-up installed"> | RD# | 21 | +5 V |
| <input type="checkbox" data-checklist-id="z80-wr" aria-label="Z80 WR# pull-up installed"> | WR# | 22 | +5 V |
| <input type="checkbox" data-checklist-id="z80-busack" aria-label="Z80 BUSACK# pull-up installed"> | BUSACK# | 23 | +5 V |
| <input type="checkbox" data-checklist-id="z80-busreq" aria-label="Z80 BUSREQ# pull-up installed"> | BUSREQ# | 25 | +5 V |

**Do not add a +5 V pull-up to RESET# pin 26.** It will be connected directly
to a Pico output. **Do not add a WAIT# pull-up at pin 24**; HCT32 drives WAIT#
in this design.

### Step 5 - Fit eight Pico data pull-downs on Peripheral

For **each row below**, fit one individual 10 kOhm resistor from the listed
empty Pico header contact to Peripheral GND. These keep the Pico-side data
inputs defined before firmware drives them. They are not pulls on the
5 V Z80 data bus.

| Done | Data bit | Pico GPIO | Physical Pico header pin | Resistor's other end |
| --- | --- | --- | ---: | --- |
| <input type="checkbox" data-checklist-id="pico-gp10" aria-label="Pico GP10 data pull-down installed"> | D0 | GP10 | 14 | GND |
| <input type="checkbox" data-checklist-id="pico-gp11" aria-label="Pico GP11 data pull-down installed"> | D1 | GP11 | 15 | GND |
| <input type="checkbox" data-checklist-id="pico-gp12" aria-label="Pico GP12 data pull-down installed"> | D2 | GP12 | 16 | GND |
| <input type="checkbox" data-checklist-id="pico-gp13" aria-label="Pico GP13 data pull-down installed"> | D3 | GP13 | 17 | GND |
| <input type="checkbox" data-checklist-id="pico-gp14" aria-label="Pico GP14 data pull-down installed"> | D4 | GP14 | 19 | GND |
| <input type="checkbox" data-checklist-id="pico-gp15" aria-label="Pico GP15 data pull-down installed"> | D5 | GP15 | 20 | GND |
| <input type="checkbox" data-checklist-id="pico-gp16" aria-label="Pico GP16 data pull-down installed"> | D6 | GP16 | 21 | GND |
| <input type="checkbox" data-checklist-id="pico-gp17" aria-label="Pico GP17 data pull-down installed"> | D7 | GP17 | 22 | GND |

Keep these resistors near the Pico/data-translator region. Later data jumpers
join these contacts to U9/U10 A pins; do not add a second set of pull-downs
at those devices.

### Step 6 - Fit seven Pico startup resistors on Peripheral

Fit one individual 10 kOhm resistor for each row. These are additional to
the eight data pull-downs above. Use the **3.3 V rail**, not +5 V, for the
four pull-ups. The 3.3 V rail remains unpowered until the Pico is fitted
in Phase 1 and supplies it from header pin 36.

| Done | Resistor | Signal / GPIO | Physical Pico header pin | Resistor's other end | Default |
| --- | --- | --- | ---: | --- | --- |
| <input type="checkbox" data-checklist-id="r21" aria-label="R21 GP2 startup pull-down installed"> | R21 | CLK output, GP2 | 4 | GND | LOW |
| <input type="checkbox" data-checklist-id="r22" aria-label="R22 GP3 startup pull-down installed"> | R22 | RESET# output, GP3 | 5 | GND | LOW, reset asserted |
| <input type="checkbox" data-checklist-id="r17" aria-label="R17 GP4 startup pull-up installed"> | R17 | BUSREQ# output, GP4 | 6 | +3.3 V | HIGH |
| <input type="checkbox" data-checklist-id="r18" aria-label="R18 GP5 startup pull-up installed"> | R18 | BOOT_READ_DISABLE, GP5 | 7 | +3.3 V | HIGH |
| <input type="checkbox" data-checklist-id="r19" aria-label="R19 GP6 startup pull-up installed"> | R19 | Upward OE# request, GP6 | 9 | +3.3 V | HIGH, path disabled |
| <input type="checkbox" data-checklist-id="r20" aria-label="R20 GP7 startup pull-up installed"> | R20 | Downward OE#, GP7 | 10 | +3.3 V | HIGH, path disabled |
| <input type="checkbox" data-checklist-id="r23" aria-label="R23 GP9 startup pull-down installed"> | R23 | IO_RELEASE, GP9 | 12 | GND | LOW |

### Step 7 - Ground the unused logic inputs

These are **direct wire links, not additional resistors**:

- Memory: U3 HCT32 socket pins **12 and 13** to GND; leave output pin 11 open.
- Core: U4 AHCT244 socket pins **15 and 17** to GND; leave outputs 5 and 3 open.

### Installation count before testing

| Board / location | Permanent resistors | Capacitors |
| --- | ---: | --- |
| Memory | 16 address pull-ups | Three: two 100 nF, one 22 uF |
| Core | Eight CPU control pull-ups | Three: two 100 nF, one 22 uF |
| Peripheral | 15: eight data pull-downs plus seven startup pulls | Four: three 100 nF, one 22 uF |
| Common supply entry | None | One 100 uF |
| **Total** | **39 individual 10 kOhm resistors** | **11 capacitors** |

Leave all permanent resistors and capacitors installed for subsequent phases.
Temporary diagnostic pulls are added only when a later phase requests them.

## Power Distribution and Isolation

- Apply external +5 V before USB; unplug USB before removing external +5 V.
  Do not operate a populated board from USB alone: U9 data ports lack `Ioff`.
  Program a removed Pico off-board if USB-only programming is needed.
- All fitted 5 V ICs must be powered together. A fitted unpowered device is
  not equivalent to an empty socket. Pull resistors do not isolate it.
- Never connect a 5 V output directly to a Pico GPIO. Both LVC devices run
  from Pico 3.3 V, not from +5 V. During ramp, asserted RESET and stopped
  clock prevent execution; RAM is indeterminate until loaded and verified.
- Arbitrary external-power loss with USB attached is not protected. Add
  hardware isolation in a separately reviewed redesign if that is required.
- Power and USB must be disconnected before changing wiring or measuring
  resistance. Stop on a collapsed rail, excessive current, or warm device.

## Test Plan

1. With everything disconnected, verify every socket power contact, diode
   polarity, capacitor polarity, and no +5 V/3.3 V/GND short. Let capacitor
   resistance readings settle; inspect any near-zero sustained resistance.
   Check each of the 39 resistor paths from its named socket/header contact
   to its specified rail: expect approximately 10 kOhm with devices absent.
   The direct GND links should have continuity, not 10 kOhm. If a resistor
   reading is unexpected, inspect parallel wiring paths and isolate one
   resistor lead with power disconnected if needed to verify its value.
2. With active devices absent, apply +5 V using a 100 mA initial limit.
   Measure 4.75-5.25 V at every 5 V socket; the unpowered 3.3 V rail must not
   be driven. Remove power if a rail droops more than 5% or the limit trips.
3. Record resistance, voltage, and unloaded current. Disconnect power and
   confirm discharge before fitting the Pico in Phase 1.

## Pass Gate

Correct polarity and supplies at all contacts, no shorts, no unexpected
current, and all startup resistors fitted. A blank board's voltage test does
not establish loaded-regulator performance; repeat measurements each phase.
