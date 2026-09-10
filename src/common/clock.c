#include "z80sbc/clock.h"

#include <limits.h>

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "z80sbc/pins.h"

static uint32_t actual_clock_hz;

bool z80_clock_set_hz(uint32_t hz) {
  if (hz < 10 || hz > 8000000)
    return false;

  uint slice_num = pwm_gpio_to_slice_num(PIN_CLK);
  uint channel = pwm_gpio_to_channel(PIN_CLK);
  uint32_t sys_clk = clock_get_hz(clk_sys);
  uint32_t best_divider = 0;
  uint32_t best_count = 0;
  uint64_t best_error = UINT64_MAX;

  pwm_set_enabled(slice_num, false);
  for (uint32_t divider = 1; divider <= 255; ++divider) {
    uint64_t denominator = (uint64_t)hz * divider;
    uint64_t rounded = ((uint64_t)sys_clk + denominator / 2u) / denominator;
    if (rounded < 2)
      rounded = 2;
    if (rounded > 65536)
      rounded = 65536;
    uint32_t first_even = (uint32_t)rounded & ~1u;
    if (first_even < 2)
      first_even = 2;

    uint32_t candidates[2] = {first_even,
                              first_even < 65536 ? first_even + 2 : first_even};
    for (size_t i = 0; i < 2; ++i) {
      uint32_t count = candidates[i];
      if (i == 1 && count == candidates[0])
        continue;
      uint64_t product = (uint64_t)divider * count;
      uint64_t target_product = (uint64_t)hz * product;
      uint64_t error = sys_clk > target_product
                           ? sys_clk - target_product
                           : target_product - sys_clk;
      uint64_t best_product = (uint64_t)best_divider * best_count;
      if (best_divider == 0 ||
          error * best_product < best_error * product ||
          (error * best_product == best_error * product &&
           count > best_count)) {
        best_divider = divider;
        best_count = count;
        best_error = error;
      }
    }
  }

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
  pwm_set_enabled(pwm_gpio_to_slice_num(PIN_CLK), true);
}

void z80_clock_one_cycle(uint32_t half_period_us) {
  z80_clock_stop();
  gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_CLK, GPIO_OUT);
  gpio_put(PIN_CLK, 0);
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 1);
  busy_wait_us_32(half_period_us);
  gpio_put(PIN_CLK, 0);
}

void z80_reset_with_clock_cycles(unsigned int cycles,
                                 uint32_t half_period_us) {
  gpio_put(PIN_RESET_N, 0);
  for (unsigned int cycle = 0; cycle < cycles; ++cycle)
    z80_clock_one_cycle(half_period_us);
  z80_clock_stop();
}