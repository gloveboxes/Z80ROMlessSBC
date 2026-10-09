#include <assert.h>
#include <stdio.h>
#include "pico/stdlib.h"
void stdio_init_all(void);
int getchar_timeout_us(uint32_t timeout_us);
#define main stage02_firmware_main
#include "../../stage02_buffers_clock/main.c"
#undef main
static bool levels[32];
static bool running;
static uint32_t clock_hz;
void gpio_init(uint pin) { (void)pin; }
void gpio_put(uint pin, bool value) { levels[pin] = value; }
bool gpio_get(uint pin) { return levels[pin]; }
void gpio_set_dir(uint pin, bool output) { (void)pin; (void)output; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void sleep_ms(uint32_t delay) { (void)delay; }
void tight_loop_contents(void) {}
void stdio_init_all(void) {}
int getchar_timeout_us(uint32_t timeout_us) { (void)timeout_us; return -1; }
bool z80_clock_set_hz(uint32_t hz) { clock_hz = hz; running = true; return true; }
uint32_t z80_clock_get_hz(void) { return clock_hz; }
void z80_clock_stop(void) { running = false; }
int main(void) {
  restore_test_outputs();
  for (unsigned step = 0; step < 6; ++step) {
    step_buffer_output();
    assert(held_step == (int)step && !running);
    assert(levels[BUFFER_INPUT_PINS[step / 2]] == (step % 2 != 0));
    assert(!levels[PIN_RESET_N] && !levels[PIN_IO_RELEASE]);
    assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  }
  step_buffer_output();
  assert(held_step == -1 && levels[PIN_BOOT_READ_DISABLE]);
  puts("PASS: held buffer steps and safe restore");
}