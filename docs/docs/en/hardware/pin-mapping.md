# 1. Pin Mapping and SRAM Wiring

Pico header and expansion reference. Step-by-step wiring lives in the
[implementation phases](../implementation/index.md).

## 1.0 Raspberry Pi Pico 2 W Header Pin Map

GPIO numbers are not physical header positions. All incoming 5 V signals
pass through the 3.3 V-powered LVC244 or LVC245, including GP27/GP28, which
are not 5 V-tolerant pads. Do not rely on GP0-GP25 FT tolerance during power-off.

| GPIO | Header pin | Signal | Connection | Startup |
| --- | ---: | --- | --- | --- |
| GP0 | 1 | BUSACK# input | U7 pin 18 | Input, no internal pull |
| GP1 | 2 | IORQ# input | U7 pin 16 | Input, no internal pull |
| GP2 | 4 | CLK output | U4 pin 2 | LOW |
| GP3 | 5 | RESET# output | Z80 pin 26 | LOW |
| GP4 | 6 | BUSREQ# output | U4 pin 4 | HIGH |
| GP5 | 7 | BOOT_READ_DISABLE | U4 pin 6 | HIGH |
| GP6 | 9 | PICO_DATA_UP_OE# | U3 pin 10 | HIGH |
| GP7 | 10 | DATA_DOWN_OE# | U10 pin 19 | HIGH |
| GP8 | 11 | SRAM bank select | U4 pin 17 -> pin 3 -> SRAM A16; R24 10 kOhm to GND at Pico pin 11; dedicated, not on J2 | LOW (bank 0) |
| GP9 | 12 | IO_RELEASE | U3 pin 5 | LOW |
| GP10-GP17 | 14,15,16,17,19,20,21,22 | D0-D7 | U9 and U10 pins 2-9 | Inputs |
| GP18 | 24 | A0 input | U7 pin 9 | Input |
| GP19 | 25 | A1 input | U7 pin 7 | Input |
| GP20 | 26 | A2 input | U7 pin 5 | Input |
| GP21 | 27 | A4 input | U7 pin 3 | Input |
| GP22 | 29 | Expansion GPIO / Phase 10 OLED SDA | J2 pin 1 | Unconfigured until OLED service |
| GP26 | 31 | Expansion GPIO / ADC0 / Phase 10 OLED SCL | J2 pin 2 | Unconfigured until OLED service |
| GP27 | 32 | RD# input | U7 pin 14 | Input |
| GP28 | 34 | WR# input | U7 pin 12 | Input |

Connect GND header pins 3,8,13,18,23,28,38 and AGND pin 33 to common GND.
Header pin 36 supplies the two LVC devices and 3.3 V pull-ups. Connect external
+5 V through a 1N5819 to VSYS pin 39; the band faces VSYS. Leave VBUS pin 40,
RUN pin 30, 3V3_EN pin 37, and ADC_VREF pin 35 open.
GP23/24/25/29 are reserved by Pico 2 W wireless hardware.

### J2 Peripheral Expansion

The schematic exposes the **two expansion GPIOs**, plus Pico 3.3 V and common
GND, on a **1x4, 2.54 mm through-hole header**. Its assigned KiCad footprint is
`Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical`, fitted on the
current PCB. GP8 is dedicated to SRAM bank selection and is not exposed. A2's OLED socket shares the GP22/GP26/3.3 V/GND connections.

| J2 pin | Net | Pico physical header pin | Use |
| ---: | --- | ---: | --- |
| 1 | `PICO_GP22` | 29 | GP22 digital GPIO / Phase 10 OLED SDA |
| 2 | `PICO_GP26` | 31 | GP26 digital GPIO or ADC0 / Phase 10 OLED SCL |
| 3 | `+3V3` | 36 | Pico 3.3 V output; not an external supply input |
| 4 | `GND` | 3,8,13,18,23,28,33,38 | Common ground |

These are **3.3 V-only peripheral connections**, with no level shifting,
protection, or LED series resistors fitted. GP22/GP26 have no fixed pulls
on the board. Do not
apply 5 V or drive them while the Pico is unpowered. Use an external
current-limiting resistor for each LED. External I2C pull-ups must go to
3.3 V, not 5 V. Any peripheral powered from J2 shares the Pico regulator's
remaining current budget with Wi-Fi, the LVC devices, and existing pulls;
do not back-feed pin 3 or assume a dedicated peripheral power allowance.

[SSD1315 status OLED](../implementation/phase-10-websocket.md#ssd1315-status-oled)
wiring is documented in Phase 10 and requires a verified 3.3 V-compatible module.
GP8 bank-select wiring is documented in [Phase 4](../implementation/phase-4-address-bus.md#sram-bank-select).
GP3 is RESET#, not a fourth spare pin. As defined in the
[RP2350 GPIO function table](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
GP22 and GP26 offer SDA functions but no hardware I2C SCL, and do not
form a hardware SPI pin set. Use software or PIO implementations
for I2C on this header, within the two-signal limit. An SPI peripheral
needs more GPIOs or a separate design change; do not borrow supervisor
bus/control pins. Keep future peripheral handling outside timing-critical
supervisor paths and qualify it separately.

## SRAM Address and Data Trunks

Use the maintained wiring checklists:

- [Phase 3: control and address-monitor taps](../implementation/phase-3-address-generator.md#wiring-control-and-port-monitor)
- [Phase 4: Z80–SRAM address trunk](../implementation/phase-4-address-bus.md#wiring-address-trunk)
- [Phase 4: A16 bank selection](../implementation/phase-4-address-bus.md#sram-bank-select)
- [Phase 5: SRAM, translators, and Pico data connections](../implementation/phase-5-data-bus.md#wiring-bidirectional-data-path)

## Memory Control

See [Phase 2 output-buffer and fixed-logic wiring](../implementation/phase-2-buffer-clock.md#wiring-output-buffer-and-fixed-logic)
and [Phase 5 supplies, directions, and enables](../implementation/phase-5-data-bus.md#supplies-directions-and-enables).
The [PIO I/O engine](address-interface.md#pio-acceleration) owns GP6-GP17
while CP/M runs in Phases 9 and 10.

## Power and Fixed Pins

[Phase 0](../implementation/phase-0-power.md#step-7-ground-the-unused-logic-inputs)
owns IC grounds, fixed pins, supplies, pull resistors, and the power sequence.
See the [native schematic](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/hardware/kicad/z80_romless_sbc.kicad_sch)
for the complete netlist.
