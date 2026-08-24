#include "z80sbc/supervisor.h"

#include "pico/stdlib.h"
#include "z80sbc/pins.h"

void output_with_initial_level(uint pin, bool level) {
  gpio_init(pin);
  gpio_put(pin, level);
  gpio_set_dir(pin, GPIO_OUT);
}

void input_with_no_pull(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_disable_pulls(pin);
}

void z80_isolate_buses(void) {
  gpio_put(PIN_ADDR_ENABLE, 0);
  gpio_put(PIN_DATA_ENABLE, 0);
}

void z80_safe_startup(void) {
  output_with_initial_level(PIN_DATA_ENABLE, 0);
  output_with_initial_level(PIN_ADDR_ENABLE, 0);
  output_with_initial_level(PIN_RESET_N, 0);
  output_with_initial_level(PIN_BUSREQ_N, 1);
  output_with_initial_level(PIN_SRAM_WE_N, 1);
  output_with_initial_level(PIN_SRAM_CE_N, 1);
  output_with_initial_level(PIN_SRAM_OE_N, 1);
  output_with_initial_level(PIN_SPI_CS_N, 1);
  output_with_initial_level(PIN_CLK, 0);
  output_with_initial_level(PIN_DATA_DIR, 0);
  input_with_no_pull(PIN_BUSACK_N);
  input_with_no_pull(PIN_IORQ_N);
  input_with_no_pull(PIN_RD_N);
  input_with_no_pull(PIN_WR_N);
  input_with_no_pull(PIN_SPI_MISO);
}

void z80_walking_output_test(const uint *pins, size_t count,
                             uint32_t dwell_ms) {
  for (size_t index = 0; index < count; ++index)
    output_with_initial_level(pins[index], false);

  for (size_t active = 0; active < count; ++active) {
    for (size_t index = 0; index < count; ++index)
      gpio_put(pins[index], index == active);
    sleep_ms(dwell_ms);
  }

  z80_safe_startup();
}