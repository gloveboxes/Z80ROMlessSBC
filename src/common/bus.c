#include "z80sbc/bus.h"

#include <stddef.h>

#include "pico/stdlib.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"

static const uint DATA_PINS[] = {
  PIN_DATA_0, PIN_DATA_1, PIN_DATA_2, PIN_DATA_3,
  PIN_DATA_4, PIN_DATA_5, PIN_DATA_6, PIN_DATA_7,
};

void z80_set_transceiver(uint oe_n, uint dir, bool direction) {
  gpio_put(oe_n, 1);
  busy_wait_us_32(1);
  gpio_put(dir, direction);
  busy_wait_us_32(1);
  gpio_put(oe_n, 0);
}

void z80_address_bus_isolate(void) {
  gpio_put(PIN_ADDR_OE_N, 1);
}

bool z80_address_bus_drive(uint16_t address) {
  z80_address_bus_isolate();
  if (!mcp23s17_write_ports((uint8_t)address, (uint8_t)(address >> 8)) ||
      !mcp23s17_set_directions(0x00, 0x00))
    return false;
  z80_set_transceiver(PIN_ADDR_OE_N, PIN_ADDR_DIR, false);
  return true;
}

bool z80_address_bus_prepare_input(void) {
  z80_address_bus_isolate();
  if (!mcp23s17_set_directions(0xFF, 0xFF))
    return false;
  z80_set_transceiver(PIN_ADDR_OE_N, PIN_ADDR_DIR, true);
  return true;
}

bool z80_address_bus_sample(uint16_t *address) {
  uint8_t low;
  uint8_t high;
  if (address == NULL || !mcp23s17_read_ports(&low, &high))
    return false;
  *address = (uint16_t)(low | ((uint16_t)high << 8));
  return true;
}

void z80_data_bus_isolate(void) {
  gpio_put(PIN_DATA_OE_N, 1);
}

void z80_data_bus_drive(uint8_t value) {
  z80_data_bus_isolate();
  for (size_t index = 0; index < 8; ++index) {
    gpio_put(DATA_PINS[index], (value >> index) & 1u);
    gpio_set_dir(DATA_PINS[index], GPIO_OUT);
  }
  z80_set_transceiver(PIN_DATA_OE_N, PIN_DATA_DIR, true);
}

void z80_data_bus_prepare_input(void) {
  z80_data_bus_isolate();
  for (size_t index = 0; index < 8; ++index) {
    gpio_init(DATA_PINS[index]);
    gpio_set_dir(DATA_PINS[index], GPIO_IN);
    gpio_disable_pulls(DATA_PINS[index]);
  }
  z80_set_transceiver(PIN_DATA_OE_N, PIN_DATA_DIR, false);
}

uint8_t z80_data_bus_sample(void) {
  uint8_t value = 0;
  for (size_t index = 0; index < 8; ++index)
    value |= (uint8_t)(gpio_get(DATA_PINS[index]) << index);
  return value;
}