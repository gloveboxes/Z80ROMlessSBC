#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  if (!z80_sram_prepare_dma())
    printf("FAIL: SRAM DMA initialization\n");
  printf("\nStage 6: AS6C1008 SRAM DMA\n");
  printf("p=pattern, c=complement pattern, m=March test\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'p')
      printf(z80_sram_pattern_test(false) ? "PASS: pattern\n" : "FAIL\n");
    else if (command == 'c')
      printf(z80_sram_pattern_test(true) ? "PASS: complement\n" : "FAIL\n");
    else if (command == 'm')
      printf(z80_sram_march_test() ? "PASS: March\n" : "FAIL\n");
    tight_loop_contents();
  }
}