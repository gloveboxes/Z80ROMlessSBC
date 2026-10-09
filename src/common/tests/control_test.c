#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
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
void gpio_init(uint pin) { preloaded[pin] = false; }
void gpio_put(uint pin, bool value) {
  levels[pin] = value;
  preloaded[pin] = true;
  assert(levels[PIN_DATA_UP_OE_N] || levels[PIN_DATA_DOWN_OE_N]);
}
bool gpio_get(uint pin) { return levels[pin]; }
uint32_t gpio_get_all(void) {
  uint32_t value = 0;
  for (uint pin = 0; pin < 32; ++pin)
    value |= (uint32_t)levels[pin] << pin;
  return value;
}
void gpio_set_dir(uint pin, bool output) { if (output) assert(preloaded[pin]); }
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled) {
  (void)pin; (void)events; (void)enabled;
}
void gpio_acknowledge_irq(uint pin, uint32_t events) { (void)pin; (void)events; }
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                       void (*callback)(uint, uint32_t)) {
  gpio_set_irq_enabled(pin, events, enabled); irq_callback = callback;
}
void busy_wait_us_32(uint32_t delay) { now_us += delay; }
void sleep_ms(uint32_t delay) { now_us += delay * 1000u; }
absolute_time_t make_timeout_time_us(uint32_t delay) { return now_us + delay; }
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
void tight_loop_contents(void) { now_us += 100000; }
bool z80_clock_set_hz(uint32_t hz) { running = hz != 0; return running; }
void z80_clock_stop(void) { running = false; }
void z80_clock_resume(void) {
  assert(!levels[PIN_IO_RELEASE]);
  assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  running = true;
}
void z80_clock_one_cycle(uint32_t half_period) {
  assert(half_period > 0 && levels[PIN_IO_RELEASE]);
  now_us += half_period * 2;
  if (release_controls)
    levels[PIN_IORQ_N] = levels[PIN_RD_N] = levels[PIN_WR_N] = true;
}
void z80_reset_with_clock_cycles(unsigned cycles, uint32_t half_period) {
  assert(cycles >= 3 && half_period > 0); gpio_put(PIN_RESET_N, 0); running = false;
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  (void)pc; (void)sp; (void)delay; ++watchdogs; longjmp(reboot_target, 1);
}
static void fixture(void) {
  memset(levels, 1, sizeof(levels));
  now_us = watchdogs = 0;
  release_controls = false;
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
  fixture();
  assert(!z80_cpu_request_bus(10) && levels[PIN_BUSREQ_N]);
  levels[PIN_BUSACK_N] = false;
  assert(z80_cpu_request_bus(10));
  assert(!z80_cpu_release_bus(10));
  assert(!levels[PIN_RESET_N] && !running);
  fixture();
  assert(z80_cpu_release_bus(10));
  for (unsigned port = 0; port < 32; ++port) {
    levels[PIN_PORT_A0] = port & 1;
    levels[PIN_PORT_A1] = port & 2;
    levels[PIN_PORT_A2] = port & 4;
    levels[PIN_PORT_A4] = port & 16;
    assert(z80_port_bus_sample() == (port & 0x17));
  }
  for (unsigned read = 0; read < 2; ++read) {
    fixture();
    levels[PIN_IORQ_N] = false;
    levels[PIN_RD_N] = !read;
    levels[PIN_WR_N] = read;
    release_controls = true;
    z80_io_trap_enable(read_port, write_port, NULL);
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
    assert(running && observed_port == 0x17 && !levels[PIN_IO_RELEASE]);
    assert_isolated();
  }
  fixture();
  levels[PIN_IORQ_N] = levels[PIN_RD_N] = false;
  z80_io_trap_enable(NULL, NULL, NULL);
  if (setjmp(reboot_target) == 0) {
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL); assert(false);
  }
  assert(watchdogs == 1 && !levels[PIN_RESET_N] && !running);
  assert_isolated();
  puts("PASS: startup, exclusive enables, port aliases, BUSACK and I/O timeout");
}