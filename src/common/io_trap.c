#include "z80sbc/io_trap.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"

enum { TRAP_RELEASE_TIMEOUT_US = 500000 };

static z80_io_read_handler_t application_read;
static z80_io_write_handler_t application_write;
static void *application_context;
static uint32_t trap_timeouts;
static uint32_t control_errors;

static _Noreturn void trap_fail_closed(void) {
  z80_cpu_fail_closed();
  z80_reset_with_clock_cycles(3, 1);
  watchdog_reboot(0, 0, 0);
  while (true)
    tight_loop_contents();
}

static void resume_and_wait_for_release(uint control_pin) {
  absolute_time_t deadline = make_timeout_time_us(TRAP_RELEASE_TIMEOUT_US);
  z80_clock_resume();
  while (!gpio_get(PIN_IORQ_N) || !gpio_get(control_pin)) {
    if (time_reached(deadline)) {
      __atomic_fetch_add(&trap_timeouts, 1, __ATOMIC_RELAXED);
      trap_fail_closed();
    }
    tight_loop_contents();
  }
}

static void io_trap_handler(uint gpio, uint32_t events) {
  (void)events;
  if (gpio != PIN_IORQ_N)
    return;
  if (!gpio_get(PIN_BUSACK_N)) {
    gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
    return;
  }

  z80_clock_stop();
  uint8_t port = 0;
  if (!z80_address_bus_prepare_input() ||
      !mcp23s17_read_port_a(&port))
    trap_fail_closed();
  z80_address_bus_isolate();

  uint32_t controls = gpio_get_all();
  bool is_read = (controls & (1u << PIN_RD_N)) == 0;
  bool is_write = (controls & (1u << PIN_WR_N)) == 0;
  if (is_read == is_write) {
    __atomic_fetch_add(&control_errors, 1, __ATOMIC_RELAXED);
    trap_fail_closed();
  }

  if (is_write) {
    z80_data_bus_prepare_input();
    uint8_t value = z80_data_bus_sample();
    if (application_write != NULL)
      application_write(port, value, application_context);
    resume_and_wait_for_release(PIN_WR_N);
    z80_data_bus_isolate();
    return;
  }

  uint8_t value = application_read == NULL
                      ? 0xFF
                      : application_read(port, application_context);
  z80_data_bus_drive(value);
  resume_and_wait_for_release(PIN_RD_N);
  z80_data_bus_isolate();
}

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
  gpio_set_irq_enabled_with_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, true,
                                     io_trap_handler);
}

void z80_io_trap_disable(void) {
  gpio_set_irq_enabled(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL, false);
  gpio_acknowledge_irq(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
}

uint32_t z80_io_trap_timeout_count(void) {
  return __atomic_load_n(&trap_timeouts, __ATOMIC_RELAXED);
}

uint32_t z80_io_trap_control_error_count(void) {
  return __atomic_load_n(&control_errors, __ATOMIC_RELAXED);
}