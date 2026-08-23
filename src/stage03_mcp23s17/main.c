#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 3: MCP23S17 SPI address generator\n");
  printf("m=register test, 1=1kHz clock, 3=1MHz clock, x=stop\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'm')
      printf(mcp23s17_register_test() ? "PASS: MCP registers\n"
                                     : "FAIL: MCP registers\n");
    else if (command == '1')
      z80_clock_set_hz(1000);
    else if (command == '3')
      z80_clock_set_hz(1000000);
    else if (command == 'x')
      z80_clock_stop();
    tight_loop_contents();
  }
}