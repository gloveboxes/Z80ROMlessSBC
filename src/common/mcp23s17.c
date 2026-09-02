#include "z80sbc/mcp23s17.h"

#include <stddef.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

enum {
  MCP_WRITE = 0x40,
  MCP_READ = 0x41,
  MCP_IODIRA = 0x00,
  MCP_IODIRB = 0x01,
  MCP_GPIOA = 0x12,
  MCP_GPIOB = 0x13,
  MCP_OLATA = 0x14,
  MCP_OLATB = 0x15,
};

bool mcp23s17_init(uint32_t spi_hz) {
  output_with_initial_level(PIN_SPI_CS_N, 1);
  spi_init(spi0, spi_hz);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);
  gpio_put(PIN_ADDR_ENABLE, 1);
  busy_wait_us_32(1);
  return true;
}

bool mcp23s17_write_register(uint8_t reg, uint8_t value) {
  uint8_t frame[] = {MCP_WRITE, reg, value};
  gpio_put(PIN_SPI_CS_N, 0);
  int written = spi_write_blocking(spi0, frame, sizeof(frame));
  gpio_put(PIN_SPI_CS_N, 1);
  return written == (int)sizeof(frame);
}

bool mcp23s17_read_register(uint8_t reg, uint8_t *value) {
  if (value == NULL)
    return false;

  uint8_t tx[] = {MCP_READ, reg, 0};
  uint8_t rx[sizeof(tx)] = {0};
  gpio_put(PIN_SPI_CS_N, 0);
  int read = spi_write_read_blocking(spi0, tx, rx, sizeof(tx));
  gpio_put(PIN_SPI_CS_N, 1);
  if (read != (int)sizeof(tx))
    return false;
  *value = rx[2];
  return true;
}

bool mcp23s17_set_directions(uint8_t port_a, uint8_t port_b) {
  return mcp23s17_write_register(MCP_IODIRA, port_a) &&
         mcp23s17_write_register(MCP_IODIRB, port_b);
}

bool mcp23s17_write_ports(uint8_t port_a, uint8_t port_b) {
  return mcp23s17_write_register(MCP_OLATA, port_a) &&
         mcp23s17_write_register(MCP_OLATB, port_b);
}

bool mcp23s17_read_port_a(uint8_t *port_a) {
  return mcp23s17_read_register(MCP_GPIOA, port_a);
}

bool mcp23s17_read_ports(uint8_t *port_a, uint8_t *port_b) {
  return mcp23s17_read_register(MCP_GPIOA, port_a) &&
         mcp23s17_read_register(MCP_GPIOB, port_b);
}

bool mcp23s17_register_test(void) {
  static const uint8_t patterns[] = {0x55, 0xAA};
  gpio_put(PIN_ADDR_ENABLE, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_ADDR_ENABLE, 1);
  busy_wait_us_32(1);
  bool passed = true;

  for (size_t index = 0; passed && index < sizeof(patterns); ++index) {
    uint8_t direction_a;
    uint8_t direction_b;
    uint8_t actual_a;
    uint8_t actual_b;
    if (!mcp23s17_set_directions(patterns[index],
                                 (uint8_t)~patterns[index]) ||
        !mcp23s17_read_register(MCP_IODIRA, &direction_a) ||
        !mcp23s17_read_register(MCP_IODIRB, &direction_b) ||
        direction_a != patterns[index] ||
        direction_b != (uint8_t)~patterns[index] ||
        !mcp23s17_write_ports(patterns[index],
                              (uint8_t)~patterns[index]) ||
        !mcp23s17_read_register(MCP_OLATA, &actual_a) ||
        !mcp23s17_read_register(MCP_OLATB, &actual_b) ||
        actual_a != patterns[index] ||
        actual_b != (uint8_t)~patterns[index])
      passed = false;
  }

  bool restored = mcp23s17_set_directions(0xFF, 0xFF);
  gpio_put(PIN_ADDR_ENABLE, 0);
  return passed && restored;
}