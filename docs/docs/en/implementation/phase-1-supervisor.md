# 8.2 Phase 1 - Raspberry Pi Pico 2 W Supervisor

**Prerequisite:** The [Phase 0 pass gate](phase-0-power.md#pass-gate) must pass.

**Install:** Pico 2 W only.

**What you are proving:** the Pico boots safely and each output reaches the
intended empty socket. Load the Stage 1 diagnostic using the
[firmware and USB-console procedure](../system/firmware-build.md#load-a-stage-and-open-its-console).
Use `s` to sample inputs and `w` for the control-output walking test.

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
    Stage 1's `PASS: safe levels restored` reports that the output sequence
    completed. It does not measure the socket voltages. The `w` command
    exercises the control outputs, not GP10-GP17; the data-GPIO check in the
    test plan is a separate required measurement/test setup.

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
monitor inputs and `w` walks the 12 control outputs. It does not walk GP10-GP17.

## Safe Startup and Walking Output (Phases 1-2)

**Maintained source:** [pins.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/pins.h),
[supervisor.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/supervisor.h), and
[supervisor.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/supervisor.c).

Preload each output latch while the pin is still an input, then enable
the output driver. This prevents a brief LOW pulse on active-low lines.

```c
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

enum {
  PIN_IORQ_N = 1, PIN_CLK = 2, PIN_RESET_N = 3,
  PIN_BUSREQ_N = 4, PIN_BUSACK_N = 0,
  PIN_DATA_DIR = 6, PIN_DATA_ENABLE = 7,
  PIN_UNUSED_8 = 8, PIN_ADDR_ENABLE = 9,
  PIN_DATA_0 = 10, PIN_DATA_1 = 11, PIN_DATA_2 = 12, PIN_DATA_3 = 13,
  PIN_DATA_4 = 14, PIN_DATA_5 = 15, PIN_DATA_6 = 16, PIN_DATA_7 = 17,
  PIN_SPI_SCK = 18, PIN_SPI_MOSI = 19, PIN_SPI_MISO = 20,
  PIN_SPI_CS_N = 21,
  PIN_SRAM_WE_N = 22, PIN_SRAM_CE_N = 5, PIN_SRAM_OE_N = 26,
  PIN_RD_N = 27, PIN_WR_N = 28
};

static void output_with_initial_level(uint pin, bool level) {
  gpio_init(pin);
  gpio_put(pin, level);       // Preload SIO output latch.
  gpio_set_dir(pin, GPIO_OUT);
}

static void input_with_no_pull(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_disable_pulls(pin);
}

static void diagnostic_safe_startup(void) {
  output_with_initial_level(PIN_DATA_ENABLE, 0);
  output_with_initial_level(PIN_ADDR_ENABLE, 0);
  output_with_initial_level(PIN_RESET_N, 0); // Hold CPU reset.
  output_with_initial_level(PIN_BUSREQ_N, 1);
  output_with_initial_level(PIN_SRAM_WE_N, 1);
  output_with_initial_level(PIN_SRAM_CE_N, 1);
  output_with_initial_level(PIN_SRAM_OE_N, 1);
  output_with_initial_level(PIN_SPI_CS_N, 1);
  output_with_initial_level(PIN_CLK, 0);
  output_with_initial_level(PIN_DATA_DIR, 0);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    input_with_no_pull(pin);
  input_with_no_pull(PIN_BUSACK_N);
  input_with_no_pull(PIN_IORQ_N); // External 10 kOhm pull-up holds this input HIGH.
  input_with_no_pull(PIN_RD_N);
  input_with_no_pull(PIN_WR_N);
  input_with_no_pull(PIN_SPI_MISO);
}

static void walking_output_test(const uint *pins, size_t count,
    uint32_t dwell_ms) {
  for (size_t index = 0; index < count; ++index)
    output_with_initial_level(pins[index], false);

  for (size_t active = 0; active < count; ++active) {
    for (size_t index = 0; index < count; ++index)
      gpio_put(pins[index], index == active);
    sleep_ms(dwell_ms);     // Probe or logic-analyzer capture point.
  }
  diagnostic_safe_startup();
}
```

Run `walking_output_test(..., 250)` only in this phase and
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
4. Before configuring GP10-GP17 as outputs, require all eight to read
  LOW from the external SIP network. Drive each HIGH in turn and verify
  3.20-3.40 V while the other seven remain LOW.
5. Run the walking-one test and probe each destination socket. Require
  one-to-one routing, 0 V/3.3 V levels, and no change on neighboring
  pins. Restore safe levels when the test ends or the USB link drops.

## Pass gate

Stable 3.3 V, safe startup levels, and correct routing
for every Pico signal.
