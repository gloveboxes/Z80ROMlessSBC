# 8.2 Phase 1 - Raspberry Pi Pico 2 Supervisor

**Prerequisite:** The [Phase 0 pass gate](phase-0-power.md#pass-gate) must pass.

**Install:** Pico 2 only.

**Firmware feature:** A diagnostic image must establish safe output
levels before enabling any GPIO output: GP7 and GP9 LOW to isolate
the data path and hold MCP RESET# asserted; GP3 LOW to assert Z80 RESET#; GP4, GP5,
GP21, GP22, and GP26 HIGH to deassert BUSREQ#, SRAM CE#, SPI CS#,
SRAM WE#, and SRAM OE#; GP2 LOW to stop the clock; and GP6 LOW for
the inactive data direction. GP8 remains an input. It must also provide a slow
walking-one GPIO test selected through the USB serial console.

**Implementation:** [Phase 1 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c),
backed by the shared [supervisor module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/supervisor.c).

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
  input_with_no_pull(PIN_BUSACK_N);
  input_with_no_pull(PIN_IORQ_N); // Section 0.3 pulls up the LVC244 input.
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
  [construction and power plan](../hardware/inventory.md#04-construction-and-power),
  USB and external power may be
  connected together. Confirm neither source back-powers the other,
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
