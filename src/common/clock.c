#include "z80sbc/clock.h"

#include <limits.h>

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "z80sbc/pins.h"

bool z80_clock_set_hz(uint32_t hz) {
  if (hz < 10 || hz > 4000000)
    return false;

  uint slice_num = pwm_gpio_to_slice_num(PIN_CLK);
  uint channel = pwm_gpio_to_channel(PIN_CLK);
  uint32_t sys_clk = clock_get_hz(clk_sys);
  uint64_t sys_clk16 = (uint64_t)sys_clk * 16u;
  uint32_t best_divider16 = 0;
  uint32_t best_count = 0;
  uint64_t best_error = UINT64_MAX;

  pwm_set_enabled(slice_num, false);
  for (uint32_t divider16 = 16; divider16 <= 4095; ++divider16) {
    uint64_t denominator = (uint64_t)hz * divider16;
    uint64_t count = (sys_clk16 + denominator / 2u) / denominator;
    if (count < 2)
      count = 2;
    if (count > 65536)
      count = 65536;

    uint64_t product = (uint64_t)divider16 * count;
    uint64_t target_product = (uint64_t)hz * product;
    uint64_t error = sys_clk16 > target_product
                         ? sys_clk16 - target_product
                         : target_product - sys_clk16;
    uint64_t best_product = (uint64_t)best_divider16 * best_count;
    if (best_divider16 == 0 ||
        error * best_product < best_error * product) {
      best_divider16 = divider16;
      best_count = (uint32_t)count;
      best_error = error;
    }
  }

  pwm_set_clkdiv(slice_num, (float)best_divider16 / 16.0f);
  pwm_set_wrap(slice_num, (uint16_t)(best_count - 1u));
  pwm_set_chan_level(slice_num, channel, (uint16_t)(best_count / 2u));
  pwm_set_counter(slice_num, 0);
  gpio_set_function(PIN_CLK, GPIO_FUNC_PWM);
  pwm_set_enabled(slice_num, true);
  return true;
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