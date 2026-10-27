#include <assert.h>
#include <stdio.h>
#include "pico/stdlib.h"

void stdio_init_all(void);
bool stdio_usb_connected(void);
int getchar_timeout_us(uint32_t timeout_us);
#define main reset_meter_firmware_main
#include "../../stage02_buffers_clock/reset_meter.c"
#undef main

static bool levels[32];
static bool outputs[32];
static bool preloaded[32];
static unsigned rising_edges;
static unsigned falling_edges;

void gpio_init(uint pin) { preloaded[pin] = false; }
void gpio_put(uint pin, bool value) {
  assert(pin != PIN_RESET_N || !value);
  assert(pin != PIN_BOOT_READ_DISABLE || value);
  assert(pin != PIN_BUSREQ_N || value);
  assert(pin != PIN_DATA_UP_OE_N || value);
  assert(pin != PIN_DATA_DOWN_OE_N || value);
  if (pin == PIN_CLK && value != levels[pin]) {
    if (value)
      ++rising_edges;
    else
      ++falling_edges;
  }
  levels[pin] = value;
  preloaded[pin] = true;
}
void gpio_set_dir(uint pin, bool output) {
  assert(!output || preloaded[pin]);
  assert(!output || pin < PIN_DATA_0 || pin > PIN_DATA_7);
  outputs[pin] = output;
}
bool gpio_get(uint pin) { return levels[pin]; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void tight_loop_contents(void) {}
void sleep_ms(uint32_t delay) { (void)delay; assert(false); }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return false; }
int getchar_timeout_us(uint32_t timeout_us) { (void)timeout_us; return -1; }

static void send_line(const char *line) {
  while (*line)
    handle_character(*line++);
}

static void assert_safe(void) {
  assert(!levels[PIN_RESET_N] && levels[PIN_BOOT_READ_DISABLE]);
  assert(levels[PIN_BUSREQ_N] && !levels[PIN_IO_RELEASE]);
  assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    assert(!outputs[pin]);
}

int main(void) {
  restart_guide();
  assert_safe();
  handle_character('n');
  assert(meter_step == STEP_PREPARATION);
  send_line("\r\n");
  assert(meter_step == STEP_POWER);
  send_line("nn\n");
  assert(meter_step == STEP_POWER);
  send_line("?\n");
  assert(meter_step == STEP_POWER);
  send_line("n\n");
  assert(meter_step == STEP_SAFE_OUTPUTS);

  for (unsigned half = 0; half < 12; ++half) {
    send_line("n\r\n");
    assert(meter_step == STEP_FIRST_CLOCK + half);
    assert(levels[PIN_CLK] == (half % 2 == 0));
    unsigned edges = rising_edges + falling_edges;
    send_line("r\nh\n?\n");
    assert(rising_edges + falling_edges == edges);
    assert_safe();
  }
  assert(rising_edges == 6 && falling_edges == 6);
  while (meter_step != STEP_FINISHED) {
    send_line("n\n");
    assert(!levels[PIN_CLK]);
    assert_safe();
  }
  send_line("n\n");
  assert(meter_step == STEP_FINISHED);
  assert(rising_edges == 6 && falling_edges == 6);

  for (unsigned step = 0; step <= STEP_FINISHED; ++step) {
    send_line("b\n");
    for (unsigned advance = 0; advance < step; ++advance)
      send_line("n\n");
    handle_character('n');
    handle_character('x');
    assert(stopped && !levels[PIN_CLK]);
    assert_safe();
    send_line("n\nn\n");
    assert(stopped && meter_step == step);
    assert(!levels[PIN_CLK]);
    send_line("r\nh\n");
    assert(stopped && !levels[PIN_CLK]);
  }
  send_line("b\n");
  assert(!stopped && meter_step == STEP_PREPARATION);
  assert_safe();
  puts("PASS: reset-held meter steps, six clocks, abort/restart and line input");
}
