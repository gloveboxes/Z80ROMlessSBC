#include "z80sbc/bus.h"

#include <stddef.h>

#include "pico/stdlib.h"
#include "z80sbc/pins.h"

static const uint DATA_PINS[] = {
  PIN_DATA_0, PIN_DATA_1, PIN_DATA_2, PIN_DATA_3,
  PIN_DATA_4, PIN_DATA_5, PIN_DATA_6, PIN_DATA_7,
};

uint8_t z80_port_bus_sample(void) {
  return (uint8_t)(gpio_get(PIN_PORT_A0) |
                   (gpio_get(PIN_PORT_A1) << 1) |
                   (gpio_get(PIN_PORT_A2) << 2) |
                   (gpio_get(PIN_PORT_A4) << 4));
}

void z80_data_bus_isolate(void) {
  gpio_put(PIN_DATA_UP_OE_N, 1);
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
}

void z80_data_bus_drive(uint8_t value) {
  z80_data_bus_isolate();
  for (size_t index = 0; index < 8; ++index) {
    gpio_put(DATA_PINS[index], (value >> index) & 1u);
    gpio_set_dir(DATA_PINS[index], GPIO_OUT);
  }
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_UP_OE_N, 0);
}

void z80_data_bus_prepare_input(void) {
  z80_data_bus_isolate();
  for (size_t index = 0; index < 8; ++index) {
    gpio_init(DATA_PINS[index]);
    gpio_set_dir(DATA_PINS[index], GPIO_IN);
    gpio_disable_pulls(DATA_PINS[index]);
  }
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_DOWN_OE_N, 0);
}

uint8_t z80_data_bus_sample(void) {
  uint8_t value = 0;
  for (size_t index = 0; index < 8; ++index)
    value |= (uint8_t)(gpio_get(DATA_PINS[index]) << index);
  return value;
}