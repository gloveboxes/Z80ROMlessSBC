#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

enum {
  MCP_IODIRA = 0x00,
  MCP_IODIRB = 0x01,
  MCP_OLATA = 0x14,
  MCP_OLATB = 0x15,
  ENDURANCE_CYCLES = 10000,
};

static void release_mcp_reset(void) {
  gpio_put(PIN_ADDR_ENABLE, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_ADDR_ENABLE, 1);
  busy_wait_us_32(1);
}

static bool output_pattern(uint16_t pattern) {
  uint8_t low;
  uint8_t high;
  if (!mcp23s17_write_ports((uint8_t)pattern, (uint8_t)(pattern >> 8)) ||
      !mcp23s17_read_ports(&low, &high))
    return false;
  printf("ports=%04x\n", pattern);
  sleep_ms(100);
  return low == (uint8_t)pattern && high == (uint8_t)(pattern >> 8);
}

static bool port_output_test(void) {
  static const uint16_t patterns[] = {0x0000, 0xFFFF, 0x5555, 0xAAAA};
  release_mcp_reset();
  if (!mcp23s17_write_ports(0x00, 0x00) ||
      !mcp23s17_set_directions(0x00, 0x00))
    return false;
  for (size_t index = 0; index < sizeof(patterns) / sizeof(patterns[0]);
       ++index) {
    if (!output_pattern(patterns[index]))
      return false;
  }
  for (unsigned int bit = 0; bit < 16; ++bit) {
    if (!output_pattern((uint16_t)(1u << bit)) ||
        !output_pattern((uint16_t)~(1u << bit)))
      return false;
  }
  bool restored = mcp23s17_set_directions(0xFF, 0xFF);
  gpio_put(PIN_ADDR_ENABLE, 0);
  return restored;
}

static bool port_input_sample(uint16_t *value) {
  uint8_t low;
  uint8_t high;
  release_mcp_reset();
  bool passed = mcp23s17_set_directions(0xFF, 0xFF) &&
                mcp23s17_read_ports(&low, &high);
  gpio_put(PIN_ADDR_ENABLE, 0);
  if (passed)
    *value = (uint16_t)(low | ((uint16_t)high << 8));
  return passed;
}

static bool register_endurance_test(void) {
  release_mcp_reset();
  for (unsigned int cycle = 0; cycle < ENDURANCE_CYCLES; ++cycle) {
    uint8_t pattern = (cycle & 1u) ? 0xAA : 0x55;
    uint8_t direction_a;
    uint8_t direction_b;
    uint8_t latch_a;
    uint8_t latch_b;
    if (!mcp23s17_set_directions(pattern, (uint8_t)~pattern) ||
        !mcp23s17_read_register(MCP_IODIRA, &direction_a) ||
        !mcp23s17_read_register(MCP_IODIRB, &direction_b) ||
        !mcp23s17_write_ports(pattern, (uint8_t)~pattern) ||
        !mcp23s17_read_register(MCP_OLATA, &latch_a) ||
        !mcp23s17_read_register(MCP_OLATB, &latch_b) ||
        direction_a != pattern || direction_b != (uint8_t)~pattern ||
        latch_a != pattern || latch_b != (uint8_t)~pattern) {
      printf("FAIL: endurance cycle %u\n", cycle);
      gpio_put(PIN_ADDR_ENABLE, 0);
      return false;
    }
  }
  bool restored = mcp23s17_set_directions(0xFF, 0xFF);
  gpio_put(PIN_ADDR_ENABLE, 0);
  return restored;
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 3: MCP23S17 SPI address generator\n");
    printf("m=register test, o=port output patterns, i=sample inputs, "
      "e=10k register cycles, 1=1kHz clock, 3=1MHz clock, x=stop\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'm')
      printf(mcp23s17_register_test() ? "PASS: MCP registers\n"
                                     : "FAIL: MCP registers\n");
    else if (command == 'o')
      printf(port_output_test() ? "PASS: port output patterns\n"
                                : "FAIL: port output patterns\n");
    else if (command == 'i') {
      uint16_t value = 0;
      printf(port_input_sample(&value) ? "inputs=%04x\n"
                                       : "FAIL: input sample\n", value);
    } else if (command == 'e')
      printf(register_endurance_test() ? "PASS: 10000 register cycles\n"
                                       : "FAIL: register endurance\n");
    else if (command == '1')
      z80_clock_set_hz(1000);
    else if (command == '3')
      z80_clock_set_hz(1000000);
    else if (command == 'x')
      z80_clock_stop();
    tight_loop_contents();
  }
}