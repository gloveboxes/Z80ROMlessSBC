#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
#include "../onboard_led.c"
#include "../terminal_network.cpp"

static unsigned lock_depth;
static bool connected;
static std::vector<uint8_t> received;
static std::vector<uint8_t> outgoing;
static unsigned disconnects;
static int32_t message_connection = -1;
static int32_t sent_connection = -1;
cyw43_t cyw43_state;
struct netif *netif_default;
static unsigned time_client_starts;
static char service_address[16];
static int init_result = PICO_OK;
static unsigned init_calls;
static unsigned led_writes;
static bool physical_led;
static bool deadline_reached;
static int led_result;
void sntp_setoperatingmode(unsigned char mode) {
  assert(lock_depth && mode == SNTP_OPMODE_POLL);
}
void sntp_setservername(unsigned char index, const char *name) {
  assert(lock_depth && index == 0 && std::string(name) == "pool.ntp.org");
}
void sntp_init() { assert(lock_depth); ++time_client_starts; }
char *ip4addr_ntoa_r(const struct ip4_addr *, char *buffer, int capacity) {
  assert(lock_depth && capacity >= 10);
  snprintf(buffer, (size_t)capacity, "192.0.2.1");
  return buffer;
}
void z80_io_ports_set_network_address(const char *address) {
  snprintf(service_address, sizeof(service_address), "%s", address);
}

void cyw43_arch_lwip_begin() { ++lock_depth; }
void cyw43_arch_lwip_end() { assert(lock_depth); --lock_depth; }
int cyw43_arch_init() { ++init_calls; return init_result; }
int cyw43_gpio_set(cyw43_t *state, int gpio, bool value) {
  assert(state == &cyw43_state);
  assert(wireless_initialized && gpio == CYW43_WL_GPIO_LED_PIN);
  ++led_writes;
  if (led_result != 0)
    return led_result;
  physical_led = value;
  return 0;
}
void cyw43_arch_deinit() {}
void cyw43_arch_enable_sta_mode() {}
int cyw43_arch_wifi_connect_async(const char *, const char *, unsigned) { return PICO_OK; }
int cyw43_tcpip_link_status(cyw43_t *, int) { return CYW43_LINK_UP; }
int cyw43_wifi_pm(cyw43_t *, int) { return PICO_OK; }
int cyw43_wifi_leave(cyw43_t *, int) { return PICO_OK; }
absolute_time_t make_timeout_time_ms(uint32_t delay) { return delay * 1000u; }
bool time_reached(absolute_time_t) { return deadline_reached; }

void z80_terminal_network_connected() { assert(lock_depth); connected = true; }
void z80_terminal_network_disconnected() {
  assert(lock_depth);
  connected = false;
  received.clear();
  outgoing.clear();
  ++disconnects;
}
void z80_terminal_network_receive(const uint8_t *data, size_t length) {
  assert(lock_depth && connected);
  received.insert(received.end(), data, data + length);
}
size_t z80_terminal_network_supply(uint8_t *data, size_t capacity) {
  assert(lock_depth && connected);
  size_t length = outgoing.size() < capacity ? outgoing.size() : capacity;
  for (size_t index = 0; index < length; ++index)
    data[index] = outgoing[index];
  outgoing.erase(outgoing.begin(), outgoing.begin() + length);
  return length;
}
void z80_terminal_network_tx_dropped(size_t) { assert(false); }

void WebSocketServer::popMessages() {
  assert(lock_depth);
  if (message_connection >= 0) {
    const uint8_t message = 'N';
    on_message(*this, (uint32_t)message_connection, &message, 1);
    message_connection = -1;
  }
}
bool WebSocketServer::close(uint32_t connection) {
  assert(lock_depth);
  on_close(*this, connection);
  return true;
}
bool WebSocketServer::sendMessage(uint32_t connection, const void *, size_t) {
  assert(lock_depth);
  sent_connection = (int32_t)connection;
  return true;
}

static void connect_client(uint32_t connection) {
  cyw43_arch_lwip_begin();
  on_connect(*server, connection);
  cyw43_arch_lwip_end();
}
static void close_client(uint32_t connection) {
  cyw43_arch_lwip_begin();
  on_close(*server, connection);
  cyw43_arch_lwip_end();
}

int main() {
  struct netif interface = {};
  netif_default = &interface;
  bool led = true;
  init_result = -1;
  assert(z80_service_led(true, &led) == IO_SERVICE_UNAVAILABLE);
  assert(init_calls == 1 && led_writes == 0);
  init_result = PICO_OK;
  assert(z80_service_led(true, &led) == IO_SERVICE_UNAVAILABLE);
  assert(init_calls == 1);
  deadline_reached = true;
  assert(z80_service_led(false, &led) == IO_SERVICE_OK && !led);
  assert(init_calls == 2 && led_writes == 1 && !physical_led);
  deadline_reached = false;
  led = true;
  assert(z80_service_led(true, &led) == IO_SERVICE_OK && physical_led);
  led = false;
  assert(z80_service_led(false, &led) == IO_SERVICE_OK && led);
  led_result = -1;
  led = false;
  assert(z80_service_led(true, &led) == IO_SERVICE_UNAVAILABLE);
  assert(physical_led);
  assert(z80_service_led(false, &led) == IO_SERVICE_OK && led);
  led_result = 0;
  if (Z80_WIFI_SSID[0] == '\0') {
    assert(!z80_terminal_network_poll());
    assert(physical_led && init_calls == 2);
    led = false;
    assert(z80_service_led(true, &led) == IO_SERVICE_OK && !physical_led);
    puts("PASS: LED control without Wi-Fi credentials");
    return 0;
  }
  assert(!z80_terminal_network_poll()); /* async association was just started */
  assert(physical_led && init_calls == 2);
  assert(z80_terminal_network_poll());
  assert(time_client_starts == 1);
  assert(std::string(service_address) == "192.0.2.1");
  assert(z80_terminal_network_poll() && time_client_starts == 1);
  led = false;
  assert(z80_service_led(true, &led) == IO_SERVICE_OK && !physical_led);
  assert(z80_service_led(false, &led) == IO_SERVICE_OK && !led);
  assert(ensure_server());
  connect_client(1);
  z80_terminal_network_poll_input();
  assert(connected);
  received = {'O'};
  outgoing = {'O'};
  close_client(1);
  connect_client(2);
  message_connection = 2;
  z80_terminal_network_poll_input();
  assert(connected && received == std::vector<uint8_t>{'N'});
  assert(outgoing.empty() && disconnects == 1);
  close_client(2);
  connect_client(3);
  close_client(3);
  z80_terminal_network_poll_input();
  assert(!connected && active_connection.load() == -1);
  connect_client(4);
  z80_terminal_network_poll_input();
  outgoing = {'O'};
  close_client(4);
  connect_client(5);
  z80_terminal_network_poll_output();
  assert(connected && outgoing.empty() && sent_connection == -1);
  outgoing = {'N'};
  z80_terminal_network_poll_output();
  assert(sent_connection == 5);
  close_client(4);
  z80_terminal_network_poll_input();
  assert(connected && active_connection.load() == 5);
  disconnect_client();
  assert(!connected && active_connection.load() == -1 && lock_depth == 0);
  delete server;
  puts("PASS: reconnect ordering, queue ownership and link disconnect");
}