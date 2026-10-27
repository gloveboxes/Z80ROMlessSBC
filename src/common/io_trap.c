#include "z80sbc/io_trap.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"
#if defined(Z80_BANKED_PIO)
#include "z80sbc/banked_pio.h"
#endif

enum { TRAP_RELEASE_TIMEOUT_US = 500000 };
static const uint32_t RESET_FAULT_MAGIC = 0x5A384954;

static z80_io_read_handler_t application_read;
static z80_io_write_handler_t application_write;
static void *application_context;
static uint32_t trap_timeouts;
static uint32_t control_errors;
static uint32_t inactive_irqs;

z80_io_trap_reset_fault_t z80_io_trap_take_reset_fault(void) {
  uint32_t magic = watchdog_hw->scratch[0];
  uint32_t reason = watchdog_hw->scratch[1];
  uint32_t gpio = watchdog_hw->scratch[2];
  uint32_t check = watchdog_hw->scratch[3];
  watchdog_hw->scratch[0] = 0;
  if (!watchdog_caused_reboot() || magic != RESET_FAULT_MAGIC ||
      check != (magic ^ reason ^ gpio) ||
      reason < Z80_IO_TRAP_RESET_RELEASE_TIMEOUT ||
      reason > Z80_IO_TRAP_RESET_INVALID_CONTROLS)
    return (z80_io_trap_reset_fault_t){0};
  return (z80_io_trap_reset_fault_t){
    .reason = (z80_io_trap_reset_reason_t)reason,
    .gpio = gpio,
  };
}

static _Noreturn void trap_fail_closed(z80_io_trap_reset_reason_t reason,
                                       uint32_t controls) {
  // Scratch 0-3 survive watchdog reset; the SDK reserves 4-7 for reboot.
  watchdog_hw->scratch[0] = 0;
  watchdog_hw->scratch[1] = reason;
  watchdog_hw->scratch[2] = controls;
  watchdog_hw->scratch[3] = RESET_FAULT_MAGIC ^ reason ^ controls;
  watchdog_hw->scratch[0] = RESET_FAULT_MAGIC;
  z80_io_trap_disable();
  z80_cpu_fail_closed();
  z80_reset_with_clock_cycles(3, 1);
  watchdog_reboot(0, 0, 0);
  while (true)
    tight_loop_contents();
}

static void __not_in_flash_func(resume_and_wait_for_release)(uint control_pin) {
  absolute_time_t deadline = make_timeout_time_us(TRAP_RELEASE_TIMEOUT_US);
  uint32_t released_mask = (1u << PIN_IORQ_N) | (1u << control_pin);
  gpio_put(PIN_IO_RELEASE, 1);
  uint32_t controls = gpio_get_all();
  if ((controls & released_mask) != released_mask) {
    z80_clock_prepare_stepping();
    do {
      if (time_reached(deadline)) {
        __atomic_fetch_add(&trap_timeouts, 1, __ATOMIC_RELAXED);
        trap_fail_closed(Z80_IO_TRAP_RESET_RELEASE_TIMEOUT, gpio_get_all());
      }
      z80_clock_step_prepared(1);
      controls = gpio_get_all();
    } while ((controls & released_mask) != released_mask);
  }
  z80_data_bus_isolate();
  busy_wait_us_32(1);
  gpio_put(PIN_IO_RELEASE, 0);
#if defined(Z80_BANKED_PIO)
  z80_banked_pio_slow_complete();
#endif
  z80_clock_resume();
}

/* Both entry paths stop CLK and retain the HIGH guard before dispatch. */
static void __not_in_flash_func(service_stopped_io)(void) {
  uint32_t controls = gpio_get_all();
  if (controls & (1u << PIN_IORQ_N)) {
    // A latched edge is not an active I/O cycle once IORQ# has released.
    __atomic_fetch_add(&inactive_irqs, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&control_errors, 1, __ATOMIC_RELAXED);
#if defined(Z80_BANKED_PIO)
    z80_banked_pio_slow_complete();
#endif
    z80_clock_resume();
    return;
  }
  bool is_read = (controls & (1u << PIN_RD_N)) == 0;
  bool is_write = (controls & (1u << PIN_WR_N)) == 0;
  if (is_read == is_write) {
    __atomic_fetch_add(&control_errors, 1, __ATOMIC_RELAXED);
    trap_fail_closed(Z80_IO_TRAP_RESET_INVALID_CONTROLS, controls);
  }
  uint8_t port = z80_port_bus_sample();

  if (is_write) {
    z80_data_bus_prepare_input();
    uint8_t value = z80_data_bus_sample();
#if defined(Z80_BANKED_PIO)
    z80_banked_pio_note_io(port, false, value);
#endif
    /* Bank-select writes are PIO-only; never switch banks in the trap. */
    if (port != 0x07 && application_write != NULL)
      application_write(port, value, application_context);
    resume_and_wait_for_release(PIN_WR_N);
    return;
  }

  uint8_t value;
  if (port == 0x07) {
    value = 0; /* No runtime banking without an active PIO backend. */
#if defined(Z80_BANKED_PIO)
    if (z80_banked_pio_active())
      value = (uint8_t)z80_memory_bank_status();
#endif
  } else {
    value = application_read == NULL ? 0xFF
              : application_read(port, application_context);
  }
#if defined(Z80_BANKED_PIO)
  z80_banked_pio_note_io(port, true, value);
#endif
  z80_data_bus_drive(value);
  resume_and_wait_for_release(PIN_RD_N);
}

static void __not_in_flash_func(io_trap_handler)(uint gpio, uint32_t events) {
  (void)events;
  if (gpio != PIN_IORQ_N)
    return;
  if (!gpio_get(PIN_BUSACK_N)) {
    gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
    return;
  }
  z80_clock_stop();
  busy_wait_us_32(1);
  service_stopped_io();
}

#if defined(Z80_BANKED_PIO)
static void __not_in_flash_func(pio_slow_handler)(void) {
  /* slow_irq_handler has already checked BUSACK, stopped CLK and guarded
   * its final HIGH phase before handing the control pins to SIO. */
  service_stopped_io();
}
#endif

bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler,
                        void *context) {
  application_read = read_handler;
  application_write = write_handler;
  application_context = context;
  z80_io_trap_rearm();
  return true;
}

void z80_io_trap_rearm(void) {
  gpio_init(PIN_IORQ_N);
  gpio_set_dir(PIN_IORQ_N, GPIO_IN);
  gpio_disable_pulls(PIN_IORQ_N);
  gpio_acknowledge_irq(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
#if defined(Z80_BANKED_PIO)
  gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
  if (z80_banked_pio_rearm(pio_slow_handler))
    return;
#endif
  gpio_set_irq_enabled_with_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, true,
                                     io_trap_handler);
}

void z80_io_trap_disable(void) {
#if defined(Z80_BANKED_PIO)
  z80_banked_pio_disable();
#endif
  gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
  gpio_acknowledge_irq(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
}

void z80_io_trap_poll(void) {
#if defined(Z80_BANKED_PIO)
  if (!z80_banked_pio_poll()) {
    __atomic_fetch_add(&trap_timeouts, 1, __ATOMIC_RELAXED);
    trap_fail_closed(Z80_IO_TRAP_RESET_RELEASE_TIMEOUT, gpio_get_all());
  }
#endif
}

uint32_t z80_io_trap_timeout_count(void) {
  return __atomic_load_n(&trap_timeouts, __ATOMIC_RELAXED);
}

uint32_t z80_io_trap_control_error_count(void) {
  return __atomic_load_n(&control_errors, __ATOMIC_RELAXED);
}

uint32_t z80_io_trap_inactive_irq_count(void) {
  return __atomic_load_n(&inactive_irqs, __ATOMIC_RELAXED);
}
