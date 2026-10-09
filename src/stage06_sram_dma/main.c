#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/cpu.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  printf("\nStage 6: Z80-assisted SRAM tests; CPU and SRAM fitted\n");
  printf("p=pattern, c=complement pattern, m=March test\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'p' || command == 'c' || command == 'm') {
      if (!z80_cpu_prepare_loader()) {
        printf("FAIL: loader initialization\n");
        continue;
      }
    }
    if (command == 'p')
      printf(z80_sram_pattern_test(false) ? "PASS: pattern\n" : "FAIL\n");
    else if (command == 'c')
      printf(z80_sram_pattern_test(true) ? "PASS: complement\n" : "FAIL\n");
    else if (command == 'm')
      printf(z80_sram_march_test() ? "PASS: March\n" : "FAIL\n");
    if (command == 'p' || command == 'c' || command == 'm')
      z80_cpu_fail_closed();
    tight_loop_contents();
  }
}