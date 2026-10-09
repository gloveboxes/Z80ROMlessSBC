#include <stdio.h>
#include "pico/stdlib.h"
#include "z80sbc/cpu.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
int main(void) {
  z80_safe_startup();
  stdio_init_all();
  printf("\nStage 4: CPU-driven address bus; Z80 and SRAM fitted\n");
  printf("a=walking addresses and readback, x=fail closed\n");
  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'a') {
      bool ok = z80_cpu_prepare_loader();
      for (unsigned bit = 0; ok && bit < 16; ++bit) {
        uint16_t address = (uint16_t)(1u << bit);
        uint8_t actual;
        ok = z80_sram_write_byte(address, (uint8_t)bit) &&
             z80_sram_read_byte(address, &actual) && actual == bit;
        printf("address=%04x verification=%s\n", address, ok ? "readback" : "FAIL");
        sleep_ms(250);
      }
      z80_cpu_fail_closed();
    } else if (command == 'x') z80_cpu_fail_closed();
    tight_loop_contents();
  }
}