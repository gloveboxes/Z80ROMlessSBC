#include <assert.h>
#include <stdio.h>
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"

static uint32_t sys_hz = 144000000;
static uint64_t now_us;
static uint64_t edge_time;
static uint64_t last_fall;
static uint64_t last_rise;
static bool pin_high;
static bool sio_high;
static bool pwm_high;
static bool output;
static bool enabled;
static uint function = GPIO_FUNC_SIO;
static uint divider;
static uint count;
static unsigned rises;
static unsigned falls;
static unsigned configuration_calls;
static unsigned sio_selections;

static void pin_level(bool high) {
  if (high == pin_high)
    return;
  assert(now_us - edge_time >= 1);
  edge_time = now_us;
  pin_high = high;
  if (high) {
    last_rise = now_us;
    ++rises;
  } else {
    last_fall = now_us;
    ++falls;
  }
}

uint32_t clock_get_hz(uint clock) { assert(clock == clk_sys); return sys_hz; }
uint pwm_gpio_to_slice_num(uint pin) { assert(pin == PIN_CLK); return 0; }
uint pwm_gpio_to_channel(uint pin) { assert(pin == PIN_CLK); return 0; }
void pwm_set_enabled(uint slice, bool on) {
  assert(slice == 0);
  enabled = on;
  if (on) {
    pwm_high = true;
    if (function == GPIO_FUNC_PWM)
      pin_level(pwm_high);
  }
}
void pwm_set_clkdiv_int_frac4(uint slice, uint8_t div, uint8_t fraction) {
  assert(slice == 0 && div > 0 && fraction == 0 && !enabled);
  divider = div;
  ++configuration_calls;
}
void pwm_set_wrap(uint slice, uint16_t wrap) {
  assert(slice == 0);
  count = (uint)wrap + 1u;
}
void pwm_set_chan_level(uint slice, uint channel, uint16_t level) {
  assert(slice == 0 && channel == 0 && level == count / 2);
}
void pwm_set_counter(uint slice, uint16_t counter) {
  assert(slice == 0 && counter == 0 && !enabled);
  // Counter writes need not update a halted output; exercise retained phase.
}
void gpio_put(uint pin, bool high) {
  assert(pin == PIN_CLK || pin == PIN_RESET_N);
  if (pin == PIN_CLK) {
    sio_high = high;
    if (function == GPIO_FUNC_SIO)
      pin_level(high);
  }
}
void gpio_set_dir(uint pin, bool out) { assert(pin == PIN_CLK); output = out; }
void gpio_set_function(uint pin, uint fn) {
  assert(pin == PIN_CLK);
  if (fn == GPIO_FUNC_SIO)
    assert(output && !sio_high && !enabled);
  else
    assert(fn == GPIO_FUNC_PWM && !enabled);
  function = fn;
  if (fn == GPIO_FUNC_SIO)
    ++sio_selections;
  pin_level(fn == GPIO_FUNC_PWM ? pwm_high : sio_high);
}
uint gpio_get_function(uint pin) { assert(pin == PIN_CLK); return function; }
void busy_wait_us_32(uint32_t delay) { assert(delay >= 1); now_us += delay; }

static void stopped_pwm(bool high) {
  function = GPIO_FUNC_PWM;
  enabled = true;
  pin_high = pwm_high = high;
  sio_high = true; // A stale SIO latch must not leak through the mux.
  edge_time = now_us;
  rises = falls = 0;
}

static void test_step(bool high) {
  stopped_pwm(high);
  uint64_t start = now_us;
  z80_clock_one_cycle(1);
  assert(now_us - start == 4);
  assert(!enabled && !pin_high && function == GPIO_FUNC_SIO);
  assert(rises == 1 && falls == (high ? 2u : 1u));
  assert(last_fall - last_rise >= 1 && now_us - last_fall >= 1);
  start = now_us;
  unsigned selections = sio_selections;
  z80_clock_one_cycle(50000);
  assert(now_us - start == 100001);
  assert(last_fall - last_rise == 50000 && now_us - last_fall == 1);
  assert(sio_selections == selections);
  unsigned old_rises = rises;
  z80_clock_resume();
  assert(enabled && function == GPIO_FUNC_PWM && rises == old_rises + 1);
}

static void test_rate(uint32_t requested) {
  stopped_pwm(true);
  assert(z80_clock_set_hz(requested));
  assert(enabled && function == GPIO_FUNC_PWM && count % 2 == 0);
  uint64_t product = (uint64_t)divider * count;
  assert(sys_hz <= (uint64_t)requested * product);
  assert(z80_clock_get_hz() <= requested);
  // Exhaustively verify the nearest permitted integer/even PWM combination.
  for (uint d = 1; d <= 255; ++d) {
    for (uint c = 2; c <= 65536; c += 2) {
      uint64_t candidate = (uint64_t)d * c;
      if (sys_hz <= (uint64_t)requested * candidate)
        assert(candidate >= product);
    }
  }
}

int main(void) {
  test_step(true);
  test_step(false);
  z80_clock_stop();
  assert(!enabled && pin_high);
  const uint32_t clocks[] = {125000000, 144000000, 150000000, 160000000};
  for (size_t i = 0; i < sizeof(clocks) / sizeof(clocks[0]); ++i) {
    sys_hz = clocks[i];
    test_rate(10);
    test_rate(1000000);
    test_rate(6500000);
    test_rate(8000000);
  }
  assert(z80_clock_get_hz() == 8000000); // 160 MHz / 20.
  sys_hz = 150000000;
  test_rate(8000000);
  assert(z80_clock_get_hz() == 7500000);
  sys_hz = 144000000;
  test_rate(8000000);
  assert(divider == 1 && count == 18 && z80_clock_get_hz() == 8000000);
  const uint32_t requested[] = {
    1000000, 1500000, 2000000, 2500000, 3000000,
    3500000, 4000000, 4500000, 5000000, 5500000,
    6000000, 6500000, 7000000, 7500000, 8000000,
  };
  const uint counts[] = {144, 96, 72, 58, 48, 42, 36, 32, 30, 28, 24, 24, 22, 20, 18};
  for (size_t i = 0; i < sizeof(requested) / sizeof(requested[0]); ++i) {
    test_rate(requested[i]);
    assert(divider == 1 && count == counts[i]);
  }
  unsigned old_calls = configuration_calls;
  assert(!z80_clock_set_hz(0) && !z80_clock_set_hz(8000001));
  assert(configuration_calls == old_calls && enabled);
  stopped_pwm(false);
  rises = 0;
  z80_reset_with_clock_cycles(6, 1);
  assert(rises == 6 && !pin_high && now_us - last_fall >= 1);
  puts("PASS: settled steps, preloaded mux, reset clocks and bounded PWM rates");
}
