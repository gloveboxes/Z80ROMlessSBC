# 8.9 Phase 8 - Integrated Virtual-ROM Boot and I/O Trap

**Prerequisite:** The [Phase 7 pass gate](phase-7-z80.md#pass-gate) must pass.

**Install:** No further chips. Load the Phase 8 supervisor firmware.

**Wiring:** Make no hardware changes. Recheck the Phase 2 IORQ#-to-WAIT#
path and the Phase 3, 5, and 7 monitor paths against the
[implementation wiring index](../hardware/bus-isolation.md#54-implementation-wiring-index)
before enabling the integrated trap.

**Firmware feature:** Combine safe startup, timed bus acquisition,
image injection and readback, run control, and the synchronous IN/OUT
trap. Maintain counters for boots, DMA failures, readback mismatches,
trap timeouts, and unexpected RD#/WR# control states.

**Implementation:** [Phase 8 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage08_virtual_io/main.c),
using the shared [I/O trap](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c),
[CPU](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/cpu.c), and
[SRAM](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/sram.c) modules.

## Synchronous I/O Trap Handler (Phase 8)

**Maintained source:** [io_trap.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/io_trap.h)
and [io_trap.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c).

A falling-edge IRQ on `PIN_IORQ_N` freezes the clock, reverses the
address transceiver with the same contention-safe helper used
elsewhere, reads the trapped port from the lower MCP23S17 port
([Section 6.4's 8-bit decode limit](../system/operation.md#64-system-performance-envelope-constraints)),
and reuses the already-tested data
bus helpers from the SRAM DMA code to sample or drive the data byte.
The handler first confirms `PIN_BUSACK_N` is still HIGH; IORQ#
tri-states along with RD#/WR# whenever BUSACK# is asserted
as described in the
[storage ownership rules](../system/operation.md#63-onboard-flash-cpm-disk-storage),
so a falling edge seen while the Pico already owns the
bus cannot be a real Z80 cycle and is ignored before any bus or SPI0
state is touched.
The GAL has already asserted WAIT# before the handler runs. Both MCP
ports must be forced to inputs before the two transceivers'
shared OE# is enabled, even though only GPIOA is read; otherwise the
still-output GPB port fights Z80 A8-A15. For `IN`, the data byte must
stay driven until the Z80 samples it, so the clock resumes and `RD#`
is polled before the data bus is isolated.
DATA_ENABLE is also the GAL's WAIT-ready input, so the selected data path
must remain enabled until IORQ# and RD#/WR# have both returned HIGH.

`process_virtual_io_read` and `process_virtual_io_write` are the only
application-supplied hooks. They must use the signatures below, finish
without sleeping, printing, waiting on USB, or taking a lock held by
main code, and must not start another bus operation. Move expensive
work to the main loop through a bounded queue. Pico SDK queue operations do
not wait for space or data, but their short internal spinlock section means
they are multicore-safe rather than lock-free.

```c
#include "hardware/watchdog.h"

uint8_t process_virtual_io_read(uint8_t port, void *context);
void process_virtual_io_write(uint8_t port, uint8_t value, void *context);

enum { TRAP_RELEASE_TIMEOUT_US = 500000 }; // Covers the 10 Hz test mode.
static volatile uint32_t trap_timeout_count;
static volatile uint32_t unexpected_control_count;

static _Noreturn void reset_after_trap_fault(void) {
  isolate_buses();
  gpio_put(PIN_RESET_N, 0);
  stop_z80_clock();
  for (unsigned int cycle = 0; cycle < 3; ++cycle)
    clock_one_cycle(1); // RESET# setup and each half-cycle exceed Z80 minima.
  watchdog_reboot(0, 0, 0); // Fail-closed: same recovery path as flash faults.
  while (true)
    tight_loop_contents(); // watchdog_reboot() takes effect asynchronously.
}

static void resume_and_wait_for_release(uint control_pin) {
  absolute_time_t deadline = make_timeout_time_us(TRAP_RELEASE_TIMEOUT_US);
  resume_z80_clock();
  while (!gpio_get(PIN_IORQ_N) || !gpio_get(control_pin)) {
    if (time_reached(deadline)) {
      ++trap_timeout_count;
      reset_after_trap_fault();
    }
    tight_loop_contents();
  }
}

static void io_trap_handler(uint gpio, uint32_t events) {
  (void)events;
  if (gpio != PIN_IORQ_N)
    return;  // Only IORQ# is ever armed, but never trust a shared callback.
  if (!gpio_get(PIN_BUSACK_N)) {
    gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
    return;  // The foreground release path re-arms this interrupt.
  }
  stop_z80_clock();
  gpio_put(PIN_ADDR_ENABLE, 1);                     // Release MCP reset.
  busy_wait_us_32(1);
  mcp_write(IODIRA, 0xFF);
  mcp_write(IODIRB, 0xFF);
  uint8_t port = mcp_read(GPIOA);                  // 8-bit I/O decode.
  gpio_put(PIN_ADDR_ENABLE, 0);                    // Reset makes all ports inputs.

  bool is_read = !gpio_get(PIN_RD_N);
  bool is_write = !gpio_get(PIN_WR_N);
  if (is_read == is_write) {      // Neither or both asserted: hardware fault.
    ++unexpected_control_count;
    reset_after_trap_fault();
  }

  if (is_write) {
    data_bus_prepare_input();                          // Bus -> Pico.
    uint8_t value = data_bus_sample();
    process_virtual_io_write(port, value, NULL);
    resume_and_wait_for_release(PIN_WR_N);
    isolate_buses();
  } else {
    data_bus_drive(process_virtual_io_read(port, NULL)); // Pico -> Bus.
    resume_and_wait_for_release(PIN_RD_N);
    isolate_buses();
  }
}

static void enable_io_trap(void) {
  input_with_no_pull(PIN_IORQ_N); // Pull-up is on the LVC244's 5 V input.
  gpio_acknowledge_irq(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
  gpio_set_irq_enabled_with_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL,
    true, &io_trap_handler);
}

static void disable_io_trap(void) {
  gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
  gpio_acknowledge_irq(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
}
```

**Test plan:**

1. On startup, the Phase 8 firmware asserts RESET#, supplies at least three
  full clocks, injects a deterministic 64 KiB image, reads back all 65,536
  bytes, arms the trap, and only then releases RESET#. Verify the Z80 address
  and data buses are high-impedance before either transceiver is enabled.
  Ctrl-] `s` reports separate boot-attempt, DMA, and readback counters.
2. Enter Ctrl-] `h` to reload the self-checking RAM increment/USB terminal
  image and run it for one hour at 1 MHz. Require the automatic completion
  result and Ctrl-] `s` to report zero RAM, trap-timeout, or control errors.
3. Enter Ctrl-] `p`. The generated Z80 self-test executes OUT on ports 0x00,
  0x01, 0x55, 0xAA, and 0xFF with matching data. Verify correct clock stop,
  address/data capture, driver disable, clock resume, and a PASS result.
4. The same Ctrl-] `p` test executes matching IN instructions, stores each
  Pico-generated byte in SRAM, reloads and compares it on the Z80, and
  reports failure through the dedicated test-result port.
5. On macOS, connect to `/dev/cu.usbmodem...`. Ordinary bytes feed the
  bounded terminal RX queue and Z80 output drains through the bounded TX
  queue; Ctrl-] introduces diagnostics. Verify echoed bytes,
  disconnect/reconnect queue clearing, full-queue counters, and no blocking
  USB call in the trap. This is an intermediate transport test, not the final
  terminal.
6. Use [DSLogic Group C](../hardware/logic-analyzer.md#group-c-trapped-io-and-data-path-interlock)
  for separate IN and OUT captures. Prove IORQ# asserts WAIT# before the
  Z80 WAIT sampling edge, DATA_ENABLE releases WAIT# only after direction
  and data are stable, and WAIT# remains released until IORQ# and the active
  RD#/WR# control are HIGH.
7. Use Group C to prove the two transceiver OE# signals are never LOW
  together. Use [Group D](../hardware/logic-analyzer.md#group-d-sram-ownership-and-control-propagation)
  to prove the CPU bus is high-impedance before DMA enable and that no
  overlap occurs between SRAM control sources.
8. Enter Ctrl-] `b` to perform 100 automated reset-held full-image
  injection/readback/execution/I/O cycles with zero failures. This is not a
  substitute for cold-power behavior: also perform ten physical power cycles
  while externally logging each startup PASS line and checking the startup
  waveforms.

## Pass gate

Zero image or boot failures, correct IN/OUT behavior,
one-hour stable execution, and contention-free ownership transitions.
