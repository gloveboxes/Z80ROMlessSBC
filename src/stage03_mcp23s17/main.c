#include <stdio.h>
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"
int main(void) {
  z80_safe_startup();
  stdio_init_all();
  printf("\nStage 3: buffered control and port inputs; CPU absent\n");
  printf("s=sample inputs, w=release WAIT, x=safe restore\n");
  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 's')
      printf("port=%02x BUSACK#=%u IORQ#=%u RD#=%u WR#=%u\n",
             z80_port_bus_sample(), gpio_get(PIN_BUSACK_N),
             gpio_get(PIN_IORQ_N), gpio_get(PIN_RD_N), gpio_get(PIN_WR_N));
    else if (command == 'w') gpio_put(PIN_IO_RELEASE, 1);
    else if (command == 'x') z80_safe_startup();
    tight_loop_contents();
  }
}