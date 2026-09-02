# 8.3 Phase 2 - ATF22V10 Arbitration and SN74AHCT244 Buffer

**Prerequisite:** The [Phase 1 pass gate](phase-1-supervisor.md#pass-gate) must pass.

**Install:** Program and verify the ATF22V10 outside the circuit, then
install it with the SN74AHCT244 and SRAM still removed. After the GAL
truth-table tests pass, install the AHCT244. Keep the Z80, MCP23S17, and
SRAM removed. Connect GAL pins 9/10/11 to GP7/GP9/GP6 with their fitted
pull-downs, connect raw IORQ# to GAL pin 13, connect GAL pin 20 to the
pulled-up Z80 WAIT# node, and tie both AHCT244
output-enable pins LOW. Require 4.75 V to 5.25 V at GAL VCC before
testing any pulled-up GAL output.

**Firmware feature:** Add commands to toggle each supervisor output at
10 Hz and generate selectable 1 kHz, 100 kHz, and 1 MHz 50% duty-cycle
clocks on GP2.

**Implementation:** [Phase 2 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/main.c),
using the shared [clock module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/clock.c).

## Variable-Frequency Clock Generation (Phase 2, Phases 7-8 Run Modes)

**Maintained source:** [clock.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/clock.h)
and [clock.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/clock.c).

Configure `PIN_CLK` as a PWM output once during Phase 2 bring-up. Reuse
the same slice for the selectable 10 Hz/1 kHz/100 kHz/1 MHz run modes in
[Phase 7](phase-7-z80.md) and [Phase 8](phase-8-virtual-io.md), and to freeze
the clock during the Phase 8 I/O trap.

```c
#include "hardware/pwm.h"
#include "hardware/clocks.h"

static bool set_z80_clock_hz(uint32_t hz) {
  if (hz < 10 || hz > 8000000)
    return false;

  uint slice_num = pwm_gpio_to_slice_num(PIN_CLK);
  pwm_set_enabled(slice_num, false);
  uint32_t sys_clk = clock_get_hz(clk_sys);
  uint64_t sys_clk16 = (uint64_t)sys_clk * 16u;
  uint32_t best_divider16 = 0;
  uint32_t best_count = 0;
  uint64_t best_error = UINT64_MAX;

  // Search all legal 8.4 dividers; this runs only when frequency changes.
  for (uint32_t divider16 = 16; divider16 <= 4095; ++divider16) {
    uint64_t denominator = (uint64_t)hz * divider16;
    uint64_t count = (sys_clk16 + denominator / 2u) / denominator;
    if (count < 2) count = 2;
    if (count > 65536) count = 65536;
    uint64_t product = (uint64_t)divider16 * count;
    uint64_t target_product = (uint64_t)hz * product;
    uint64_t error = sys_clk16 > target_product ?
      sys_clk16 - target_product : target_product - sys_clk16;
    uint64_t best_product = (uint64_t)best_divider16 * best_count;
    if (best_divider16 == 0 ||
      error * best_product < best_error * product) {
      best_divider16 = divider16;
      best_count = (uint32_t)count;
      best_error = error;
    }
  }

  uint16_t wrap = (uint16_t)(best_count - 1u);
  pwm_set_clkdiv(slice_num, (float)best_divider16 / 16.0f);
  pwm_set_wrap(slice_num, wrap);
  pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PIN_CLK),
    (uint16_t)(best_count / 2u)); // Exact 50% when count is even.
  pwm_set_counter(slice_num, 0);
  gpio_set_function(PIN_CLK, GPIO_FUNC_PWM);
  pwm_set_enabled(slice_num, true);
  return true;
}

static void stop_z80_clock(void) {
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), false); // May freeze HIGH or LOW.
}

static void resume_z80_clock(void) {
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), true);
}
```

**Test plan:**

1. Require a successful programmer readback/verify of the exact
  `src/pld/sram_control.pld` JEDEC image before inserting the GAL.
2. With RESET# LOW, toggle Pico CE#/OE#/WE# one at a time and require
  the corresponding GAL pin 16/15/14 to follow while the other two
  remain HIGH. Then set RESET# HIGH and manually exercise pulled-up
  BUSACK#/MREQ#/RD#/WR# through 1 kOhm; verify all three CPU-side truth
  table paths and the BUSACK# LOW override.
3. With each Pico and Z80 candidate control held HIGH, toggle RESET#
  and BUSACK# separately while observing GAL pins 14-16 on the scope.
  The consensus terms must hold every output continuously HIGH; any
  active-low pulse fails the programmed image.
4. **GAL installed; data transceivers removed:** Hold DATA_ENABLE pin 9
  LOW and toggle DATA_DIR pin 11; GAL pins 17
  and 18 must both remain HIGH. Drive DATA_ENABLE HIGH: pin 17 must be
  LOW only when DATA_DIR is HIGH, and pin 18 must be LOW only when
  DATA_DIR is LOW. Scope both outputs while changing DATA_DIR with
  DATA_ENABLE LOW; neither may pulse LOW.
5. Hold DATA_ENABLE LOW. Drive the pulled-up pin-13 IORQ# test node LOW
  through 1 kOhm and require GAL pin 20 / WAIT# LOW. Raise DATA_ENABLE
  and require WAIT# HIGH; release IORQ# and require WAIT# to remain HIGH
  for either DATA_ENABLE state. Scope IORQ#-to-WAIT# assertion and
  DATA_ENABLE-to-WAIT# release; any glitch or inverted case fails.
6. Install the AHCT244. Use the Stage 2 walking command to toggle its
  eight functional input paths independently; RESET# was already tested
  at the [Phase 1 pass gate](phase-1-supervisor.md#pass-gate) and is not an
  AHCT244 input. At the selected output require
  LOW below 0.3 V, correct polarity, and no activity on adjacent outputs.
  Require each non-clock HIGH to reach at least 4.4 V. At Z80 CLK pin 6,
  require HIGH to reach at least the simultaneously measured Z80
  $V_{CC}-0.5$ V, which gives 100 mV margin above the Z84C00
  $V_{IHC}=V_{CC}-0.6$ V minimum across the permitted rail range.
  Use command `t` to toggle each path independently at 10 Hz and command
  `w` for the walking-output capture.
7. Test AHCT244 channel 1A1 at each clock frequency. Require 45% to 55%
  duty cycle and clean transitions at Z80 socket pin 6.
8. After the Pico 3.3 V rail reaches 3.20 V, verify RESET# remains LOW
  while BUSREQ#, SRAM CE#/WE#/OE#, and SPI CS# remain HIGH for at least
  100 ms. Before 3.3 V is valid, RESET# must remain LOW but SRAM control
  levels are not used as a retention guarantee.
9. Power-cycle ten times while monitoring these signals. Any active-low
  transition after 3.3 V becomes valid fails the phase.

## Pass gate

Programmer verification and every GAL truth-table case
pass, all eight AHCT244 outputs have valid 5 V levels and correct
polarity, the 1 MHz clock is clean, and startup creates no active-low
glitch.
