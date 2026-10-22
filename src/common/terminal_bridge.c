#include "z80sbc/terminal.h"
#include "weather_http.h"

bool z80_terminal_network_poll(void);
void z80_terminal_network_poll_input(void);
void z80_terminal_network_poll_output(void);

void z80_terminal_core1_service(void) {
  if (z80_terminal_network_poll()) {
    z80_terminal_network_poll_input();
    z80_terminal_network_poll_output();
  }
  z80_weather_network_poll();
}
