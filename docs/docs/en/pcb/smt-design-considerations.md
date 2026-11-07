# Partial-SMT PCB Design Considerations

These constraints apply to the **98.3 x 60 mm** board in
`hardware/kicad-smt/`. Do not apply the
[DIP layout's](design-considerations.md) package, socket or mounting
dimensions to it. Both boards implement the same assisted-loader circuit,
but require separate physical qualification.

## Replacement SRAM and voltage domains

U2 is **IS62C1024AL-35QLI**, a 128K x 8, 35 ns SRAM in a 450-mil
SOP-32 package. The [ISSI datasheet](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/datasheets/IS62C1024AL.pdf)
covers the exact QLI part. Its SOP pin assignment matches the
AS6C1008-55PCN DIP SRAM used on the other board:

| SRAM pin | Connection |
| --- | --- |
| 1, NC | Unconnected |
| 2, A16 | U4.3, buffered bank select from Pico GP8 through U4.17 |
| 16, GND | Common GND |
| 22, CE1# | U4.12, buffered Z80 MREQ# |
| 24, OE# | U3.3, buffered RD# OR buffered boot-read-disable |
| 29, WE# | U4.9, buffered Z80 WR# |
| 30, CE2 | +5V, permanently HIGH |
| 32, VDD | +5V |

The ISSI supply range is **4.5-5.5 V**. Its TTL-compatible inputs specify
HIGH at 2.2 V minimum and LOW at 0.8 V maximum; the Alliance SRAM uses
a 0.7 x VCC HIGH threshold. Existing AHCT buffering on A16 and memory
controls is retained. Do not connect Pico GPIOs directly to the 5V buses.

The 35 ns SRAM has different access, output-enable and hold times from
the 55 ns part. In particular, minimum output hold after an address change
is 3 ns rather than 10 ns. A faster access specification is not proof of
read/write turnaround, CPU setup/hold margin or 20 MHz operation.

U1/U2/U3/U4/U9 use +5V. U7/U10 use Pico-derived +3V3, with 5.5 V-tolerant
inputs facing the Z80 bus and 3.3 V outputs facing the Pico. U9 DIR is HIGH
(Pico to bus); U10 DIR is LOW (bus to Pico). The shared
[pin map](../hardware/pin-mapping.md) gives the complete signal assignments.

## Layer stack and routing

| Layer | Role |
| --- | --- |
| F.Cu | Signals and via-free clock routes |
| In1.Cu | GND plane, no signal tracks |
| In2.Cu | +5V plane, no signal tracks |
| B.Cu | Signals |

The board uses a 1.6 mm FR-4 stack, with 1 oz outer and 0.5 oz inner copper.
Default signals are 0.30 mm with 0.20 mm clearance; the clock clearance is
0.30 mm. The +5V/VSYS power netclass uses 0.60 mm tracks and +3V3 uses
0.40 mm. USB-C CC tracks use 0.15 mm. Signal vias are 0.80 mm with 0.40 mm
drills. Confirm the manufacturer's actual stack and capabilities.

The Pico module is reflowed on its castellated pads. Its micro-USB end
faces the bottom edge and its antenna lies above an enclosed copper/FR-4
window. A **4 mm FR-4 bridge** retains the top edge, with a copper keepout.
Do not route across the antenna void or place metal, wiring or enclosure
walls in its reserved space. Geometry does not qualify Wi-Fi performance.

The generator retains the Pico underside landing pads as unconnected
copper and keeps routed tracks clear of them. Low-profile SMT parts may
sit below the OLED; verify the real module/socket height rather than
treating a 3D render as a clearance measurement.

### Measured route lengths

The [SMT route report](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad-smt/reports/route-lengths.json)
records planar copper paths and layer changes, not measured flight times.

| Source/receiver group | Path range |
| --- | ---: |
| Z80 to SRAM address | 27.01-81.10 mm |
| Z80 to SRAM data | 25.05-43.44 mm |
| Pico-write transceiver to SRAM | 43.99-84.51 mm |
| SRAM to Pico-read transceiver | 35.53-77.73 mm |
| Pico to U9 | 9.65-59.11 mm |
| Pico to U10 | 9.17-44.33 mm |

The Z80 clock path is 39.5 mm and via-free. SRAM CE#/OE#/WE# paths
are 36.4/30.9/40.7 mm. Longer paths to inspect during qualification include
buffered RD# from U4 to U3 at 88.4 mm with four layer changes, and U9-to-SRAM
D0/D1 at 84.5/83.3 mm; D1 has six layer changes.
Passing layout budgets is not timing or signal-integrity qualification.

## Assembly preview and mechanical checks

Use the generated **JLC CPL**, not the uncorrected KiCad positions file.
Its corrections are specific to the selected JLC component models:

| Package / part | CPL rotation correction | Check in JLC preview |
| --- | --- | --- |
| SOIC/SOP ICs | +270 degrees | U2/U3/U4/U7/U9/U10 pin 1 |
| SMT electrolytics | 0 degrees | C8-C11 positive lead on +5V |
| USB-C C393939 | 0 degrees, pad-centred position | Mouth toward board edge; CPL position 1.5 mm inboard of KiCad origin |
| Pico 2 W C42394205 | +270 degrees | Micro-USB toward bottom edge, antenna over window |
| SMA diode and 0603 parts | 0 degrees | D1 cathode toward VSYS; correct passive values |

Dry-fit the purchased USB-C receptacle before ordering. Its four plated
shell slots are 0.85 mm wide, with 1.9 mm or 1.75 mm lengths.
Confirm that the quote includes these shell legs in the SMT assembly.
Verify the OLED's socket-tail fit and 5.0 mm socket body.

Four 2.7 mm NPTH holes accept M2.5 fasteners, with centres 3.2 mm from the
board edges and **91.9 x 53.6 mm** spacing. Check enclosure hardware,
underside lead lengths and support height; these are not the DIP board's
mounting coordinates.

The generator enforces solder-mask bridges, track-to-mask clearance,
silkscreen and through-hole-to-SMT spacing. Follow the detailed
[JLCDFM rules](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad-smt/README.md#jlcdfm-design-rules)
and do not waive DRC warnings to accept a route.

## Power and startup safety

J1 is a power-only USB-C receptacle, not a regulator or USB-PD trigger.
CC1 and CC2 each have a 5.1k sink resistor to GND. VBUS feeds the +5V rail;
SS14 D1 feeds Pico VSYS with its cathode toward VSYS. Pico VBUS is not
connected to the main-board supply.

J3 shares J1's rail without source isolation: **use only one external
supply at a time**. Pico's regulator is the only +3V3 source for U7/U10,
the startup pulls and OLED.

!!! warning "No USB-only operation"
    Apply external +5V before connecting Pico USB, and disconnect Pico USB
    before removing external power. U9's AHCT245 data pins lack power-off
    isolation; the VSYS diode does not isolate GPIOs. All fitted 5V ICs
    must be powered together.

Both data paths default disabled and boot-read inhibit defaults HIGH.
Clock, reset, bank select and I/O release default LOW. Inspect the
factory assembly, supply rails, reset/inhibit/enables and pin-1/polarity
before running SRAM tests.

## Hardware qualification

Start at **1 MHz**, then qualify higher rates separately for this PCB.
Check the actual address-valid, CE#/OE#/WE#, read-sampling and write-end
edges against the ISSI SRAM and CPU specifications, buffer/gate delays
and receive-pin waveforms.

Test both SRAM banks, bank switching, assisted loading, reset/recovery,
I/O WAIT, clock stop/resume, CP/M disk traffic and sustained USB/network
operation. Use the [DHO814](../hardware/oscilloscope.md) for analogue levels
and timing, and the [DSLogic Plus](../hardware/logic-analyzer.md) for bus
ordering. The breadboard's incremental chip-installation procedure is
not an assembly sequence for this populated SMT board.
