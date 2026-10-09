#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static bool levels[32];
static bool preloaded[32];
static bool running;
static bool release_controls;
static uint64_t now_us;
static jmp_buf reboot_target;
static void (*irq_callback)(uint, uint32_t);
static uint8_t observed_port;
static unsigned watchdogs;
static uint64_t release_deadline;
static uint64_t isolation_time;
static uint64_t reset_release_time;
static bool release_pending;
static bool outputs[32];
static unsigned io_clocks;
static unsigned expected_io_clocks;
static unsigned falling_edges;
static uint clock_function;
static unsigned reset_releases;
void gpio_init(uint pin) { preloaded[pin] = false; }
void gpio_put(uint pin, bool value) {
  bool previous = levels[pin];
  levels[pin] = value;
  preloaded[pin] = true;
  assert(levels[PIN_DATA_UP_OE_N] || levels[PIN_DATA_DOWN_OE_N]);
  if (pin == PIN_DATA_DOWN_OE_N && value)
    isolation_time = now_us;
  if (pin == PIN_RESET_N && value && !previous) {
    reset_release_time = now_us;
    ++reset_releases;
  }
  if (pin == PIN_CLK && value && !previous)
    assert(!release_pending);
  if (pin == PIN_CLK && !value && previous) {
    ++falling_edges;
    if (levels[PIN_RESET_N] && levels[PIN_IO_RELEASE]) {
      assert(!release_pending);
      ++io_clocks;
      if (release_controls && io_clocks == expected_io_clocks) {
        release_pending = true;
        release_deadline = now_us + 1;
      }
    }
  }
}
bool gpio_get(uint pin) { return levels[pin]; }
uint32_t gpio_get_all(void) {
  uint32_t value = 0;
  for (uint pin = 0; pin < 32; ++pin)
    value |= (uint32_t)levels[pin] << pin;
  return value;
}
void gpio_set_dir(uint pin, bool output) {
  if (output) {
    assert(preloaded[pin]);
    if (pin >= PIN_DATA_0 && pin <= PIN_DATA_7 && !outputs[pin])
      assert(now_us - isolation_time >= 1);
  }
  outputs[pin] = output;
}
void gpio_set_function(uint pin, uint function) {
  assert(pin == PIN_CLK);
  if (function == GPIO_FUNC_SIO)
    assert(!levels[PIN_CLK] && outputs[PIN_CLK]);
  else {
    assert(function == GPIO_FUNC_PWM && !release_pending);
    assert(!levels[PIN_IO_RELEASE]);
    assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
    assert(now_us - isolation_time >= 1);
  }
  clock_function = function;
}
uint gpio_get_function(uint pin) { assert(pin == PIN_CLK); return clock_function; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled) {
  (void)pin; (void)events; (void)enabled;
}
void gpio_acknowledge_irq(uint pin, uint32_t events) { (void)pin; (void)events; }
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                       void (*callback)(uint, uint32_t)) {
  gpio_set_irq_enabled(pin, events, enabled); irq_callback = callback;
}
void busy_wait_us_32(uint32_t delay) {
  now_us += delay;
  if (release_pending && now_us >= release_deadline) {
    levels[PIN_IORQ_N] = levels[PIN_RD_N] = levels[PIN_WR_N] = true;
    release_pending = false;
  }
}
void sleep_ms(uint32_t delay) { now_us += delay * 1000u; }
absolute_time_t make_timeout_time_us(uint32_t delay) { return now_us + delay; }
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
void tight_loop_contents(void) { now_us += 100000; }
uint32_t clock_get_hz(uint clock) { assert(clock == clk_sys); return 144000000; }
uint pwm_gpio_to_slice_num(uint pin) { assert(pin == PIN_CLK); return 0; }
uint pwm_gpio_to_channel(uint pin) { assert(pin == PIN_CLK); return 0; }
void pwm_set_enabled(uint slice, bool enabled) {
  assert(slice == 0);
  if (enabled)
    assert(levels[PIN_RESET_N] && now_us - reset_release_time >= 1);
  running = enabled;
}
void pwm_set_clkdiv_int_frac4(uint slice, uint8_t divider, uint8_t fraction) {
  assert(slice == 0 && divider > 0 && fraction == 0 && !running);
}
void pwm_set_wrap(uint slice, uint16_t wrap) { (void)wrap; assert(slice == 0); }
void pwm_set_chan_level(uint slice, uint channel, uint16_t level) {
  (void)level; assert(slice == 0 && channel == 0);
}
void pwm_set_counter(uint slice, uint16_t counter) {
  assert(slice == 0 && counter == 0 && !running);
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  (void)pc; (void)sp; (void)delay; ++watchdogs; longjmp(reboot_target, 1);
}
static void fixture(void) {
  memset(levels, 1, sizeof(levels));
  memset(outputs, 0, sizeof(outputs));
  now_us = watchdogs = 0;
  io_clocks = falling_edges = 0;
  release_pending = release_controls = running = false;
  isolation_time = reset_release_time = 0;
  expected_io_clocks = 1;
  clock_function = GPIO_FUNC_SIO;
  reset_releases = 0;
  z80_safe_startup();
}
static void assert_isolated(void) {
  assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
}
static uint8_t read_port(uint8_t port, void *context) {
  (void)context; observed_port = port; return 0xA5;
}
static void write_port(uint8_t port, uint8_t value, void *context) {
  (void)value; (void)context; observed_port = port;
}
int main(void) {
  fixture();
  assert_isolated();
  assert(!levels[PIN_RESET_N] && levels[PIN_BOOT_READ_DISABLE]);
  assert(!levels[PIN_IO_RELEASE] && levels[PIN_BUSREQ_N]);
  z80_data_bus_drive(0xA5);
  assert(!levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  z80_data_bus_prepare_input();
  assert(levels[PIN_DATA_UP_OE_N] && !levels[PIN_DATA_DOWN_OE_N]);
  z80_data_bus_drive(0x5A);
  assert(!levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  fixture();
  assert(!z80_cpu_request_bus(10) && levels[PIN_BUSREQ_N]);
  levels[PIN_BUSACK_N] = false;
  assert(z80_cpu_request_bus(10));
  assert(!z80_cpu_release_bus(10));
  assert(!levels[PIN_RESET_N] && !running);
  fixture();
  assert(z80_cpu_release_bus(10));
  fixture();
  assert(z80_cpu_release_reset_and_run(1000000));
  assert(running && levels[PIN_RESET_N] && falling_edges == 7);
  fixture();
  assert(!z80_cpu_release_reset_and_run(8000001));
  assert(!running && !levels[PIN_RESET_N] && levels[PIN_BOOT_READ_DISABLE]);
  assert(reset_releases == 0);
  assert(!z80_cpu_release_reset_and_run(0) && reset_releases == 0);
  for (unsigned port = 0; port < 32; ++port) {
    levels[PIN_PORT_A0] = port & 1;
    levels[PIN_PORT_A1] = port & 2;
    levels[PIN_PORT_A2] = port & 4;
    levels[PIN_PORT_A4] = port & 16;
    assert(z80_port_bus_sample() == (port & 0x17));
  }
  for (unsigned steps = 1; steps <= 3; ++steps) {
    for (unsigned read = 0; read < 2; ++read) {
      fixture();
      levels[PIN_RESET_N] = true;
      expected_io_clocks = steps;
      levels[PIN_IORQ_N] = false;
      levels[PIN_RD_N] = !read;
      levels[PIN_WR_N] = read;
      release_controls = true;
      z80_io_trap_enable(read_port, write_port, NULL);
      irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
      assert(running && observed_port == 0x17 && !levels[PIN_IO_RELEASE]);
      assert_isolated();
      assert(io_clocks == steps && !release_pending);
    }
  }
  fixture();
  levels[PIN_IORQ_N] = levels[PIN_RD_N] = false;
  z80_io_trap_enable(NULL, NULL, NULL);
  if (setjmp(reboot_target) == 0) {
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL); assert(false);
  }
  assert(watchdogs == 1 && !levels[PIN_RESET_N] && !running);
  assert_isolated();
  puts("PASS: turnaround, reset setup, delayed I/O release, port aliases and faults");
}