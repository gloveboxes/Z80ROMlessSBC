#include "z80sbc/clock.h"

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "z80sbc/pins.h"

static uint32_t actual_clock_hz;

static void select_stopped_sio(void) {
  z80_clock_stop();
  gpio_put(PIN_CLK, 0);
  gpio_set_dir(PIN_CLK, GPIO_OUT);
  if (gpio_get_function(PIN_CLK) != GPIO_FUNC_SIO) {
    // Extend a frozen PWM HIGH before the mux can create a falling edge.
    busy_wait_us_32(1);
    gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
  }
}

bool z80_clock_set_hz(uint32_t hz) {
  if (hz < Z80_CLOCK_MIN_HZ || hz > Z80_CLOCK_MAX_HZ)
    return false;

  uint slice_num = pwm_gpio_to_slice_num(PIN_CLK);
  uint channel = pwm_gpio_to_channel(PIN_CLK);
  uint32_t sys_clk = clock_get_hz(clk_sys);
  uint32_t best_divider = 0;
  uint32_t best_count = 0;

  for (uint32_t divider = 1; divider <= 255; ++divider) {
    uint64_t denominator = (uint64_t)hz * divider;
    uint64_t count = ((uint64_t)sys_clk + denominator - 1u) / denominator;
    count = (count + 1u) & ~UINT64_C(1);
    if (count < 2)
      count = 2;
    if (count > 65536)
      continue;
    uint64_t product = (uint64_t)divider * count;
    uint64_t best_product = (uint64_t)best_divider * best_count;
    if (best_divider == 0 || product < best_product ||
        (product == best_product && count > best_count)) {
      best_divider = divider;
      best_count = (uint32_t)count;
    }
  }
  if (best_divider == 0)
    return false;

  select_stopped_sio();
  busy_wait_us_32(1);
  pwm_set_clkdiv_int_frac4(slice_num, (uint8_t)best_divider, 0);
  pwm_set_wrap(slice_num, (uint16_t)(best_count - 1u));
  pwm_set_chan_level(slice_num, channel, (uint16_t)(best_count / 2u));
  pwm_set_counter(slice_num, 0);
  gpio_set_function(PIN_CLK, GPIO_FUNC_PWM);
  pwm_set_enabled(slice_num, true);
  uint64_t product = (uint64_t)best_divider * best_count;
  actual_clock_hz = (uint32_t)(((uint64_t)sys_clk + product / 2u) / product);
  return true;
}

uint32_t z80_clock_get_hz(void) {
  return actual_clock_hz;
}

void z80_clock_stop(void) {
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), false);
}

void z80_clock_resume(void) {
  pwm_set_counter(pwm_gpio_to_slice_num(PIN_CLK), 0);
  gpio_set_function(PIN_CLK, GPIO_FUNC_PWM);
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), true);
}

void z80_clock_one_cycle(uint32_t half_period_us) {
  select_stopped_sio();
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 1);
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 0);
  // Let CPU controls, translators and input synchronizers settle before return.
  busy_wait_us_32(1);
}

void z80_reset_with_clock_cycles(unsigned int cycles,
                                 uint32_t half_period_us) {
  gpio_put(PIN_RESET_N, 0);
  for (unsigned int cycle = 0; cycle < cycles; ++cycle)
    z80_clock_one_cycle(half_period_us);
  z80_clock_stop();
}