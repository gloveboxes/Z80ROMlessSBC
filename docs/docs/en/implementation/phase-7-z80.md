# 8.8 Phase 7 - Z84C0020PEC CPU, Installed Last

**Prerequisite:** The [Phase 6 pass gate](phase-6-sram.md#pass-gate) must pass.

**Install:** Z84C0020PEC. Preload SRAM first. Disable both bus
transceivers, drive the Pico-side SRAM CE#/OE#/WE# controls inactive,
hold RESET# LOW and BUSREQ# HIGH, and stop the clock LOW before
insertion and power-up. RESET# LOW deliberately keeps the programmed
logic on the inactive Pico side; the GAL equations switch to CPU controls
automatically only when RESET# is released with BUSACK# HIGH.

**Firmware feature:** Add clock single-step and selectable 10 Hz, 1 kHz,
100 kHz, and 1 MHz run modes; reset pulse control; timed BUSREQ#/BUSACK#
acquisition; and a command that preloads and verifies a small test
program before releasing reset.

**Implementation:** [Phase 7 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage07_z80_cpu/main.c),
using the shared [CPU ownership module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/cpu.c).

## Z80 Single-Step and Timed Bus Request (Phases 7-8)

**Maintained source:** [cpu.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/cpu.h)
and [cpu.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/cpu.c).

Single-step uses SIO rather than PWM. `request_cpu_bus()` and
`release_cpu_bus()` each take an explicit `timeout_us` bound so a
wiring fault or a missing Z80 cannot hang the supervisor waiting on
BUSACK#. The clock must be running while requesting or releasing BUSREQ#;
after using `clock_one_cycle()`, call `set_z80_clock_hz()` to restore
the CLK pin's PWM function before either handshake.

```c
static void clock_one_cycle(uint32_t half_period_us) {
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), false);
  gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_CLK, GPIO_OUT);
  gpio_put(PIN_CLK, 0);
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 1);
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 0);
}

static bool request_cpu_bus(uint32_t timeout_us) {
  absolute_time_t deadline = make_timeout_time_us(timeout_us);
  isolate_buses();
  gpio_put(PIN_BUSREQ_N, 0);
  while (gpio_get(PIN_BUSACK_N) != 0) {
    if (time_reached(deadline)) {
      gpio_put(PIN_BUSREQ_N, 1);
      return false;
    }
    tight_loop_contents();
  }
  return true;               // RESET# HIGH: BUSACK# LOW selects DMA controls.
}

static bool release_cpu_bus(uint32_t timeout_us) {
  absolute_time_t deadline = make_timeout_time_us(timeout_us);
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  isolate_buses();
  gpio_put(PIN_BUSREQ_N, 1);
  while (gpio_get(PIN_BUSACK_N) == 0) {
    if (time_reached(deadline)) {
      gpio_put(PIN_RESET_N, 0);
      return false;
    }
    tight_loop_contents();
  }
  return true;               // BUSACK# HIGH restores CPU controls.
}
```

**Test plan:**

1. Verify RESET# LOW, BUSREQ# HIGH, both transceiver OE# signals HIGH,
  and SRAM write inactive immediately after power-up.
2. Clock at 10 Hz with RESET# asserted for at least three full cycles.
  Use command `0`, release reset, and verify the first opcode fetch at
  0x0000, including
  the expected M1#, MREQ#, and RD# sequence.
3. Preload `NOP; NOP; JP 0000h`. Single-step and verify address
  progression with `l` followed by repeated `s` commands while Pico bus
  drivers stay high-impedance.
4. Repeat with commands `1`, `2`, and `3` for 1 kHz, 100 kHz, and 1 MHz
  respectively, with no unexpected SRAM writes.
5. Use command `q` to assert BUSREQ# while clocking. Require BUSACK# LOW after the current
  machine cycle and verify CPU bus outputs are high-impedance. Release
  BUSREQ# and require execution to resume.
6. Use command `z` to assert RESET# for at least three clocks during
  execution, release it, and verify a new fetch from 0x0000.

## Pass gate

Correct reset fetch and execution through 1 MHz, valid
BUSREQ#/BUSACK# transfer, and no bus contention.
