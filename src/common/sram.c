#include "z80sbc/sram.h"

#include <stddef.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static bool report_failure(uint16_t address, uint8_t expected,
                           uint8_t actual) {
  printf("FAIL %04x expected=%02x actual=%02x\n", address, expected, actual);
  z80_isolate_buses();
  return false;
}

bool z80_sram_prepare_dma(void) {
  z80_isolate_buses();
  gpio_put(PIN_RESET_N, 0);
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  return mcp23s17_set_directions(0x00, 0x00);
}

bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  if (!z80_address_bus_drive(address))
    return false;
  z80_data_bus_drive(value);
  gpio_put(PIN_SRAM_CE_N, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_SRAM_WE_N, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_SRAM_WE_N, 1);
  gpio_put(PIN_SRAM_CE_N, 1);
  return true;
}

bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  if (value == NULL)
    return false;

  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  if (!z80_address_bus_drive(address))
    return false;
  z80_data_bus_prepare_input();
  gpio_put(PIN_SRAM_CE_N, 0);
  gpio_put(PIN_SRAM_OE_N, 0);
  busy_wait_us_32(1);
  *value = z80_data_bus_sample();
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_CE_N, 1);
  z80_data_bus_isolate();
  return true;
}

bool z80_sram_load(uint16_t address, const uint8_t *data, uint32_t length) {
  if (data == NULL || length > 65536u - address)
    return false;
  for (uint32_t index = 0; index < length; ++index) {
    if (!z80_sram_write_byte((uint16_t)(address + index), data[index]))
      return false;
  }
  z80_isolate_buses();
  return true;
}

bool z80_sram_verify(uint16_t address, const uint8_t *data, uint32_t length) {
  if (data == NULL || length > 65536u - address)
    return false;
  for (uint32_t index = 0; index < length; ++index) {
    uint8_t actual = 0;
    if (!z80_sram_read_byte((uint16_t)(address + index), &actual) ||
        actual != data[index])
      return report_failure((uint16_t)(address + index), data[index], actual);
  }
  z80_isolate_buses();
  return true;
}

bool z80_sram_pattern_test(bool complement) {
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    if (complement)
      expected = (uint8_t)~expected;
    if (!z80_sram_write_byte((uint16_t)address, expected))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    uint8_t actual = 0;
    if (complement)
      expected = (uint8_t)~expected;
    if (!z80_sram_read_byte((uint16_t)address, &actual) || actual != expected)
      return report_failure((uint16_t)address, expected, actual);
  }
  z80_isolate_buses();
  return true;
}

static bool march_read_write(uint16_t address, uint8_t expected,
                             uint8_t replacement) {
  uint8_t actual = 0;
  if (!z80_sram_read_byte(address, &actual) || actual != expected)
    return report_failure(address, expected, actual);
  return z80_sram_write_byte(address, replacement);
}

bool z80_sram_march_test(void) {
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!z80_sram_write_byte((uint16_t)address, 0x00))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!march_read_write((uint16_t)address, 0x00, 0xFF))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!march_read_write((uint16_t)address, 0xFF, 0x00))
      return false;
  }
  for (uint32_t address = 65536u; address-- > 0;) {
    if (!march_read_write((uint16_t)address, 0x00, 0xFF))
      return false;
  }
  for (uint32_t address = 65536u; address-- > 0;) {
    if (!march_read_write((uint16_t)address, 0xFF, 0x00))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t actual = 0;
    if (!z80_sram_read_byte((uint16_t)address, &actual) || actual != 0x00)
      return report_failure((uint16_t)address, 0x00, actual);
  }
  z80_isolate_buses();
  return true;
}