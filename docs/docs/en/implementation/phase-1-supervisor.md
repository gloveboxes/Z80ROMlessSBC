# 8.2 Phase 1 - Raspberry Pi Pico 2 W Supervisor

**Prerequisite:** The [Phase 0 pass gate](phase-0-power.md#pass-gate) must pass.

**Install:** Pico 2 W only.

**What you are proving:** the Pico boots safely and each output reaches the
intended empty socket. Load the Stage 1 diagnostic using the
[firmware and USB-console procedure](../system/firmware-build.md#load-a-stage-and-open-its-console).
Use `s` to sample GPIO levels, `w` for the control-output walking test,
`r` to toggle RESET# HIGH/LOW,
`d` to single-step the eight data outputs, and `x` to restore safe levels.

A **walking one** drives one tested output HIGH at a time while the others
are LOW. Probe the named destination and its neighbors: only the intended
line should change. A walking zero reverses the pattern. These tests reveal
swapped, shorted, and unconnected wires more clearly than toggling everything
together.

Before running this test, install and start a USB serial terminal such as
`picocom` using the
[macOS console commands](../system/firmware-build.md#macos-console-with-picocom).
With Stage 1 running and the destination chips still absent, type `s` to
confirm the console responds, then type `w` (no Enter required) to start one
walking-one sequence across the 12 control outputs. Type `w` again to repeat
the sequence while probing another destination.

!!! note "Software PASS is not the phase pass gate"
    - `walking 12 outputs` means the firmware started the control-output test.
    - `PASS: safe levels restored` means the sequence finished and the firmware
      reapplied the safe GPIO configuration. It does not measure voltages or
      prove correct wiring.
    - `s` reports the instantaneous levels at the Pico input pins, not a wiring
      test. With the SN74LVC244 absent and internal pulls disabled, BUSACK#,
      IORQ#, RD#, WR#, and MISO can float. All-zero readings alone neither
      prove a fault nor confirm correct routing; do not expect valid bus
      status while these sockets are empty.
    - Each walking-test HIGH lasts only 250 ms. Repeat `w` while probing each
      destination and its neighbors; use a scope rather than relying on a
      slow multimeter to catch the pulse.
    - `w` does not test GP10-GP17. Use `d` for the
      [single-step data-pin test](#single-step-data-pin-test) below: each
      selected pin stays HIGH until the next `d`, `x`, `w`, or USB disconnect.
      Console messages do not replace voltage measurements.
    - Use `r` to toggle only RESET# HIGH or LOW for measurement. This
      deliberately overrides RESET#'s safe LOW level; use `x` afterward.
      All other ICs must be absent.

## Wiring - Pico 2 W

1. After the Phase 0 pass gate, disconnect external +5 V and USB. Keep both
  disconnected throughout installation, wiring, and continuity checks.
2. Before fitting the Pico, build and load the **Stage 1 supervisor**
  diagnostic from `src/stage01_supervisor/main.c`, using the commands in
  [Stage 1 firmware](#stage-1-firmware) below. Program it off the breadboard,
  then disconnect USB. Fit the Pico 2 W now, before adding signal jumpers
  that would obstruct access. Follow the
  [package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1),
  with the USB connector toward the top of the Peripheral board. Leave all
  other IC sockets empty.
3. With the Pico fitted but unpowered, install and continuity-check every
  Pico connection shown below. Check each destination socket contact
  against its pin number and check for accidental wire bridges between
  adjacent GPIOs or to the +5 V rail. The fitted Pico's circuitry can affect
  resistance readings; investigate unexpected readings rather than treating
  every continuity beep as a wiring short.

Leave the Pico fitted for the test plan. Apply power only after the wiring
checks are complete, using the power sequence in test step 1.

### Wire Length and Routing

**Use short, direct wires; do not make every wire the same length.** Choose
the shortest practical route with enough slack to avoid pulling on contacts.
Do not add loops or coils to shorter wires to match a longer wire. The
[breadboard placement plan](../hardware/construction.md#board-roles-and-placement)
is a routing aid, not a length-matching requirement.

| Connection group below | Routing guidance |
| --- | --- |
| Pico to SN74AHCT244: CLK | Keep the Pico-to-buffer wire short with a nearby common-GND return connection. In Phase 2, keep the buffer-to-Z80 CLK wire especially short and entirely on the Core board. |
| Pico to SN74AHCT244: SCK, MOSI, CS# | Keep the SPI wires short and direct; precise length matching is unnecessary. Avoid long parallel runs of SCK beside other signals. |
| Pico to SN74AHCT244: BUSREQ# | No length matching is required. Keep the route direct and away from CLK. |
| Pico to ATF22V10 and Z80 RESET# | No length matching is required for the GAL controls or shared RESET# node. Keep routes direct and away from CLK. |
| SN74LVC244 to Pico | No length matching is required for BUSACK#, IORQ#, RD#, WR#, or SPI MISO (MCP SO). Minimize unnecessary wire length, particularly on MISO. |
| Pico D0-D7 to SN74AHCT245 and SN74LVC245 | Use compact routes with short branches to both transceivers. Similar lengths are convenient, but exact matching is unnecessary; avoid dangling wire ends. |
| Pico 3.3 V and GND connections | Length matching is irrelevant. Prioritize short supply connections, solid common-GND distribution, and the specified local decoupling capacitors. |

A few centimetres of wire-length difference contributes little arrival-time
skew for this design. Long breadboard jumpers are more concerning because
they can cause ringing and coupling into neighboring wires. Logic edges
remain fast even when the clock frequency is low: short routes and nearby
ground returns matter more than equal lengths. The later scope checks, not
wire-length matching, establish whether the clock and bus signals are clean.

{%
  include-markdown "../hardware/pin-mapping.md"
  start='<template id="phase-1-pico-wiring">'
  end="phase-1-pico-wiring-end</template>"
%}

Leave GP8/header pin 11 open. Connect the Pico ground pins and leave the
remaining unused header pins open as specified in the
[complete Pico pin map](../hardware/pin-mapping.md#10-raspberry-pi-pico-2-w-header-pin-map).

### Pico 2 W startup behavior

- Once the Pico 3.3 V rail is valid, the external resistors establish safe
  levels before firmware configures SIO.
- During a cold power ramp, Pico-side pull-ups cannot hold active-low
  controls HIGH while the 3.3 V rail is still at 0 V.
- RESET# therefore remains asserted, and SRAM contents remain indeterminate
  until the boot image is loaded and verified.

**Firmware feature:** A diagnostic image must establish safe output
levels before enabling any GPIO output: GP7 and GP9 LOW to isolate
the data path and hold MCP RESET# asserted; GP3 LOW to assert Z80 RESET#; GP4, GP5,
GP21, GP22, and GP26 HIGH to deassert BUSREQ#, SRAM CE#, SPI CS#,
SRAM WE#, and SRAM OE#; GP2 LOW to stop the clock; and GP6 LOW for
the inactive data direction. With the data interface disabled, initialize
GP10-GP17 as SIO inputs before the first SRAM write; changing only SIO direction
does not clear RP2350's reset-time pad isolation or select the SIO function.
GP8 remains an input. It must also provide a slow walking-one GPIO test
selected through the USB serial console.

**Application source:** [src/stage01_supervisor/main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c),
backed by the shared [supervisor module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/supervisor.c).

### Stage 1 firmware

Use only the **Stage 1 supervisor** image for this phase:

- **Application source:** [src/stage01_supervisor/main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c).
- **Build target:** `z80_stage01_supervisor`.
- **UF2 to load:** `build/src/stage01_supervisor/z80_stage01_supervisor.uf2`.

After completing the one-time
[toolchain and CMake setup](../system/firmware-build.md#71-firmware-and-cpm-build),
run this from the repository root:

```sh
cmake --build build --target z80_stage01_supervisor -j
```

This requires the configured repository-root `build/` tree, not an empty
directory created with `mkdir`. If you are working inside
`src/stage01_supervisor`, use the
[stage-local configure and build commands](../system/firmware-build.md#building-from-the-stage-1-directory)
instead; that build tree has a different UF2 path.

With the Pico off the breadboard, hold **BOOTSEL** while connecting its USB
data cable, then release the button. Load the built image and reboot:

```sh
picotool load -v build/src/stage01_supervisor/z80_stage01_supervisor.uf2
picotool reboot
```

Disconnect USB before fitting the Pico and wiring it. After the wiring checks,
follow test step 1's power sequence, then open the USB serial console using the
[console procedure](../system/firmware-build.md#load-a-stage-and-open-its-console).
The startup banner is `Z80 ROMless SBC - Stage 1 supervisor`; `s` samples the
monitor and data GPIOs, `w` walks the 12 control outputs, `d` single-steps
GP10-GP17, `r` toggles RESET# only, and `x` restores
safe levels.

### RESET# Toggle Test

**Phase 1 only: keep every IC socket except the Pico empty.** These commands
can release Z80 reset; do not use this test on a populated system.
The Pico produces the voltage:
do not connect a GPIO to either supply rail to force its level.

Type `x` to establish safe levels. With the meter's black lead on common GND,
use the red probe on each RESET# contact listed below. All three are the same
electrical node, so they should measure the same voltage. Type `r` to toggle
RESET#. Expect these voltages at **each** contact:

| Probe point | RESET# LOW | RESET# HIGH |
| --- | ---: | ---: |
| Pico header pin 5 (GP3) | 0 V | 3.20-3.40 V (3.3 V) |
| Z80 socket pin 26 | 0 V | 3.20-3.40 V (3.3 V) |
| ATF22V10 socket pin 1 | 0 V | 3.20-3.40 V (3.3 V) |

The shared RESET# node must never exceed the Pico 3.3 V rail; no 5 V pull-up
is permitted. A digital GPIO read cannot detect overvoltage. Repeat `r` as
needed; each press flips only GP3, leaving all other GPIOs unchanged. Scope
the node if checking edges or overshoot.

Type `x` to finish and restore **all** safe levels, including RESET# LOW. USB
disconnect or starting `w` also restores safe levels. A new `d` sequence
restores safe levels before driving D0; `r` does not advance or end an already
active data-pin test. `s` leaves the pin levels unchanged.

**This test checks routing and commanded HIGH/LOW levels, not startup.**
After typing `x`, verify the inactive levels listed above with a meter.
To check before, during, and after startup, arm the scope and repeat the
normal cold-power sequence: disconnect USB before removing external +5 V,
then apply external +5 V before reconnecting USB. Capture GP3 and Z80 RESET#
through the ramp and firmware startup; separately capture GP7 and GP9 and
the other controls as required. Do not issue `r`, `d`, or `w` during
these startup captures. Firmware cannot observe the interval before it runs,
and console readings cannot prove the RESET# voltage limit or absence of
short pulses.

### Single-Step Data-Pin Test

**Use this test only in Phase 1, with the Pico fitted and all other IC sockets
empty.** The firmware produces the HIGH voltage; do not jumper a GPIO to the
3.3 V rail. Keep DATA_ENABLE (GP7) and ADDR_ENABLE (GP9) LOW throughout.

- Set the multimeter to DC volts. Connect its black lead to common circuit
  GND and use the red probe on the named header or empty socket contact.
  Avoid bridging adjacent contacts; do not change wiring while powered.
- Type `x`, then `s` (no Enter required). GP10-GP17 are inputs, and the
  `Data GPIO levels` line must show D0-D7 all zero. Measure all eight LOW
  before starting; the external RN3 pull-down network establishes these
  levels, not an internal Pico pull-down.
- Type `d` once. The firmware checks that all eight data inputs read LOW,
  configures all eight as outputs initially LOW, then holds **D0/GP10 HIGH**.
  If it reports `NOT STARTED`, investigate RN3 and the wiring before retrying;
  it has left the data GPIOs as inputs.
- Measure **3.20-3.40 V** at the selected Pico header pin and at the matching
  A-port contact of **both empty transceiver sockets** in the table below.
  Confirm the other seven data lines remain LOW and neighboring contacts
  do not change. Take as long as needed; there is no automatic step timer.
- Type `d` again to lower the previous pin and hold the next one HIGH.
  Repeat through **D7/GP17**. Each step prints the GPIO, physical Pico header
  pin, and transceiver socket pin to probe. `s` samples levels without
  advancing or ending the test.
- After measuring D7, type `d` once more to finish, or type `x` at any point
  to stop. The firmware restores safe levels and returns GP10-GP17 to inputs.
  A USB-console disconnect also ends an active data test; reconnect and
  start again with `d`. Typing `w` first ends the data test, then runs the
  separate control-output sequence.

| Selected data line | Pico GPIO | Pico header pin | AHCT245 A-port socket contact | LVC245 A-port socket contact |
| --- | --- | --- | --- | --- |
| D0 | GP10 | 14 | A1, pin 2 | A1, pin 2 |
| D1 | GP11 | 15 | A2, pin 3 | A2, pin 3 |
| D2 | GP12 | 16 | A3, pin 4 | A3, pin 4 |
| D3 | GP13 | 17 | A4, pin 5 | A4, pin 5 |
| D4 | GP14 | 19 | A5, pin 6 | A5, pin 6 |
| D5 | GP15 | 20 | A6, pin 7 | A6, pin 7 |
| D6 | GP16 | 21 | A7, pin 8 | A7, pin 8 |
| D7 | GP17 | 22 | A8, pin 9 | A8, pin 9 |

## Safe Startup and Walking Output (Phases 1-2)

### Stage 1 Console and Data-Stepping Source

**Maintained source:** [Stage 1 main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c).

The `r`, `d`, `x`, `s`, and `w` commands, pin testing, and
USB-disconnect cleanup are implemented in the Stage 1 application below.
This listing is included directly from `src/stage01_supervisor/main.c` when MkDocs builds
the page, so it follows changes to the actual application source.

```c
{% include "../../../../src/stage01_supervisor/main.c" %}
```

### Shared Safe-Startup and Walking Helpers

**Maintained source:** [pins.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/pins.h),
[supervisor.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/supervisor.h), and
[supervisor.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/supervisor.c).

These helpers are shared by the phase applications; the Stage 1 console
commands are in the application listing above, not in this shared module.
Preload each output latch while the pin is still an input, then enable
the output driver. This prevents a brief LOW pulse on active-low lines.
The following listing is included directly from `src/common/supervisor.c`
when MkDocs builds the page; it is not a separately maintained example.

```c
{% include "../../../../src/common/supervisor.c" %}
```

Run `z80_walking_output_test(..., 250)` only in this phase and
[Phase 2](phase-2-buffer-clock.md) while the destination driver/bus chips are
absent. It deliberately changes raw pin levels and is not safe as an in-system
diagnostic after [Phase 3](phase-3-address-generator.md).

**Test plan:**

1. With the 1N5819 fitted as specified in the
  [Phase 0 power plan](phase-0-power.md#power-distribution-and-isolation),
  apply external +5 V before connecting USB, and disconnect USB before
  removing external +5 V. Both sources may be connected together.
  Confirm neither source back-powers the other,
  then require 3.20 V to 3.40 V on the Pico 3.3 V rail, at the
  still-absent SN74LVC245AN and SN74LVC244AN VCC contacts.
2. Scope GP7 and GP9 through reset and startup; GP7 must remain LOW and
  GP9 must remain LOW so both bus interfaces stay isolated.
3. Verify GP3 and the directly connected Z80 RESET# socket pin are LOW,
  and all other
  Pico control pins have the inactive levels listed above before,
  during, and after startup. Verify the RESET# node never exceeds the
  Pico 3.3 V rail; no 5 V pull-up is permitted on it.
  Use the [RESET# toggle test](#reset-toggle-test)
  for separate routing and voltage checks, then type `x` and perform the
  startup captures without intentionally toggling the pins.
4. Before configuring GP10-GP17 as outputs, require all eight to read
  LOW from the external SIP network. Follow the
  [single-step data-pin test](#single-step-data-pin-test): use `d` to drive
  each HIGH in turn and verify 3.20-3.40 V while the other seven remain LOW.
  Finish with `x` or one more `d` after D7 to return the data GPIOs to inputs.
5. Run the walking-one test and probe each destination socket. Require
  one-to-one routing, 0 V/3.3 V levels, and no change on neighboring
  pins. Restore safe levels when the test ends or the USB link drops.

## Pass gate

**Console output alone does not complete Phase 1.** Complete every measurement
in the test plan above before proceeding:

- Measure **3.20-3.40 V** at the SN74LVC244AN and SN74LVC245AN VCC socket
  contacts, and confirm the specified power sequence causes no back-powering.
- Scope **GP7 and GP9 through reset and startup**; both must remain LOW.
- Verify **GP3 and the Z80 RESET# socket contact are LOW**, RESET# never
  exceeds the Pico 3.3 V rail, and all other control outputs have their
  specified safe startup levels.
- Run `w` repeatedly while checking **every control-output destination and
  neighboring pins**. Confirm the intended pin alone changes, with
  0 V/3.3 V levels, and safe levels return afterward.
- Check **GP10-GP17 separately**: all eight must initially read LOW, then
  use `d` to verify each reaches 3.20-3.40 V at both transceiver A-port socket
  contacts while the other seven stay LOW. Restore safe levels with `x` or
  finish the sequence. Do not mark this complete based on the 12-output `w`
  test.
