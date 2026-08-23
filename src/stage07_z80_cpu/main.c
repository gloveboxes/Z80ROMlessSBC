#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint8_t CPU_TEST_PROGRAM[] = {
  0x00,             /* NOP */
  0x00,             /* NOP */
  0xC3, 0x00, 0x00, /* JP 0000h */
};

static bool load_and_start(uint32_t clock_hz) {
  if (!z80_cpu_load_and_verify(CPU_TEST_PROGRAM, sizeof(CPU_TEST_PROGRAM)))
    return false;
  return z80_cpu_release_reset_and_run(clock_hz);
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 7: Z80 CPU execution and bus ownership\n");
  printf("r=load/run 1MHz, q=BUSREQ round-trip, x=fail closed\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'r') {
      printf(load_and_start(1000000) ? "PASS: CPU running\n"
                                     : "FAIL: load/start\n");
    } else if (command == 'q') {
      bool acquired = z80_cpu_request_bus(500000);
      bool released = acquired && z80_cpu_release_bus(500000);
      printf(acquired && released ? "PASS: bus round-trip\n"
                                  : "FAIL: bus round-trip\n");
    } else if (command == 'x') {
      z80_cpu_fail_closed();
      printf("CPU held in reset\n");
    }
    tight_loop_contents();
  }
}