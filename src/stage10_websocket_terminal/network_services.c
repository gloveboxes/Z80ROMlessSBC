#include "network_runtime.h"
#include "web_terminal.h"
#include "../common/weather_http.h"

void z80_network_core1_service(void) {
  bool online = z80_network_poll();
  z80_web_terminal_poll(online, z80_network_generation());
  z80_weather_network_poll();
}
