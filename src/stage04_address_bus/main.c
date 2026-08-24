#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"

static void output_address_patterns(void) {
  static const uint16_t patterns[] = {0x0000, 0xFFFF, 0x5555, 0xAAAA};
  for (size_t index = 0; index < sizeof(patterns) / sizeof(patterns[0]);
       ++index) {
    z80_address_bus_drive(patterns[index]);
    printf("address=%04x\n", patterns[index]);
    sleep_ms(250);
  }
  for (unsigned int bit = 0; bit < 16; ++bit) {
    z80_address_bus_drive((uint16_t)(1u << bit));
    sleep_ms(100);
  }
  z80_address_bus_isolate();
  printf("PASS: patterns complete; verify on analyzer\n");
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 4: direct MCP address bus and reset isolation\n");
  printf("m=MCP test, a=output patterns, i=sample address, x=isolate\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'm') {
      printf(mcp23s17_register_test() ? "PASS: MCP registers\n" : "FAIL\n");
    } else if (command == 'a') {
      output_address_patterns();
    } else if (command == 'i') {
      uint16_t address = 0;
      bool ok = z80_address_bus_prepare_input() &&
                z80_address_bus_sample(&address);
      z80_address_bus_isolate();
      printf(ok ? "address=%04x\n" : "FAIL: address sample\n", address);
    } else if (command == 'x') {
      z80_address_bus_isolate();
    }
    tight_loop_contents();
  }
}