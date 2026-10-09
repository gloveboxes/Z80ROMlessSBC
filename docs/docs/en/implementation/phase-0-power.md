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

Fit seven local 100 nF capacitors, one at each DIP supply, three 22 uF board
bulk capacitors and one 100 uF supply-entry capacitor, rated at least 10 V.
Keep leads short; electrolytic positive leads go to +5 V.

| Individual 10 kOhm resistors | Rail at one end of each resistor | Signal at the other end |
| --- | --- | --- |
| 8 address pulls | +5 V | A0-A7, one resistor per bit |
| 8 address pulls | +5 V | A8-A15, one resistor per bit |
| 8 Pico data pulls | GND | Pico GP10-GP17, one resistor per bit |
| 8 CPU control pulls | +5 V | BUSREQ#, BUSACK#, MREQ#, IORQ#, RD#, WR#, INT#, NMI#, one resistor per signal |
| R17, 10 kOhm | +3.3 V | GP4 BUSREQ# |
| R18, 10 kOhm | +3.3 V | GP5 BOOT_READ_DISABLE |
| R19, 10 kOhm | +3.3 V | GP6 upward OE# request |
| R20, 10 kOhm | +3.3 V | GP7 downward OE# |
| R21, 10 kOhm | GND | GP2 CLK |
| R22, 10 kOhm | GND | GP3 RESET# |
| R23, 10 kOhm | GND | GP9 IO_RELEASE |

Fit **39 individual 10 kOhm resistors**, not resistor-network packages.
Each signal has its own resistor. Never join bus bits.
RESET# must have no +5 V pull-up. WAIT# is driven by U3; no additional WAIT
pull-up is fitted. Tie U3 inputs 12/13 and U4 inputs 15/17 to GND.

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
2. With active devices absent, apply +5 V using a 100 mA initial limit.
   Measure 4.75-5.25 V at every 5 V socket; the unpowered 3.3 V rail must not
   be driven. Remove power if a rail droops more than 5% or the limit trips.
3. Record resistance, voltage, and unloaded current. Disconnect power and
   confirm discharge before fitting the Pico in Phase 1.

## Pass Gate

Correct polarity and supplies at all contacts, no shorts, no unexpected
current, and all startup resistors fitted. A blank board's voltage test does
not establish loaded-regulator performance; repeat measurements each phase.
