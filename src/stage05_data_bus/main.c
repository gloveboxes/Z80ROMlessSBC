#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"

static void output_data_patterns(void) {
  static const uint8_t patterns[] = {0x00, 0xFF, 0x55, 0xAA};
  for (size_t index = 0; index < sizeof(patterns); ++index) {
    z80_data_bus_drive(patterns[index]);
    printf("data=%02x\n", patterns[index]);
    sleep_ms(250);
  }
  for (unsigned int bit = 0; bit < 8; ++bit) {
    z80_data_bus_drive((uint8_t)(1u << bit));
    sleep_ms(100);
  }
  z80_data_bus_isolate();
  printf("PASS: patterns complete; verify on analyzer\n");
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 5: LVC245 data transceiver\n");
  printf("d=output patterns, i=sample data, x=isolate\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'd') {
      output_data_patterns();
    } else if (command == 'i') {
      z80_data_bus_prepare_input();
      printf("data=%02x\n", z80_data_bus_sample());
      z80_data_bus_isolate();
    } else if (command == 'x') {
      z80_isolate_buses();
    }
    tight_loop_contents();
  }
}