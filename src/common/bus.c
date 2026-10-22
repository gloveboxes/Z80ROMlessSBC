#include "z80sbc/bus.h"

#include "pico/stdlib.h"
#include "z80sbc/pins.h"

enum { DATA_MASK = 0xFFu << PIN_DATA_0 };
_Static_assert(PIN_DATA_7 == PIN_DATA_0 + 7, "data GPIOs must be contiguous");
_Static_assert(PIN_PORT_A1 == PIN_PORT_A0 + 1 &&
                   PIN_PORT_A2 == PIN_PORT_A0 + 2 &&
                   PIN_PORT_A4 == PIN_PORT_A0 + 3,
               "port GPIO snapshot mapping changed");

uint8_t z80_port_bus_sample(void) {
  uint32_t pins = gpio_get_all() >> PIN_PORT_A0;
  return (uint8_t)((pins & 7u) | ((pins & 8u) << 1));
}

void z80_data_bus_isolate(void) {
  gpio_put(PIN_DATA_UP_OE_N, 1);
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
}

void z80_data_bus_drive(uint8_t value) {
  z80_data_bus_isolate();
  busy_wait_us_32(1);
  gpio_put_masked(DATA_MASK, (uint32_t)value << PIN_DATA_0);
  gpio_set_dir_masked(DATA_MASK, DATA_MASK);
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_UP_OE_N, 0);
}

void z80_data_bus_prepare_input(void) {
  z80_data_bus_isolate();
  gpio_set_dir_masked(DATA_MASK, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_DOWN_OE_N, 0);
}

uint8_t z80_data_bus_sample(void) {
  return (uint8_t)(gpio_get_all() >> PIN_DATA_0);
}