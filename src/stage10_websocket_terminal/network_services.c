#include "network_runtime.h"
#include "matrix_display.h"
#include "web_terminal.h"
#include "../common/weather_http.h"
#include "../common/chat.h"
#include "../common/http_io.h"

static bool service_terminal(void) {
  bool online = z80_network_poll();
  z80_matrix_set_wifi_connected(online);
  z80_web_terminal_poll(online, z80_network_generation());
  return online;
}

void z80_network_terminal_core1_service(void) {
  (void)service_terminal();
}

void z80_network_core1_service(void) {
  bool online = service_terminal();
  z80_weather_network_poll();
  z80_chat_network_service(online, z80_network_generation());
  z80_http_network_service(online, z80_network_generation());
}
