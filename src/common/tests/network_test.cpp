#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../onboard_led.c"
#include "../../stage10_websocket_terminal/network_runtime.cpp"
#include "../../stage10_websocket_terminal/web_terminal.cpp"

static unsigned lock_depth;
static bool connected;
static std::vector<uint8_t> received;
static std::vector<uint8_t> outgoing;
static unsigned disconnects;
cyw43_t cyw43_state;
struct netif *netif_default;
static unsigned time_client_starts;
static char service_address[16];
static const char *assigned_address = "192.0.2.1";
static int init_result = PICO_OK;
static unsigned init_calls;
static unsigned led_writes;
static bool physical_led;
static uint64_t now;
static int led_result;
static unsigned polls;
static int fake_link = CYW43_LINK_UP;
static bool send_ok = true;
static bool close_ok = true;
static unsigned sends;
static unsigned flushes;
static bool flush_ok = true;
static std::vector<uint8_t> sent;
static std::vector<std::vector<uint8_t>> sent_frames;
static std::vector<uint32_t> closed;
static std::vector<uint8_t> challenge;
static z80_network_config_t test_config = {"test", "password", "192.0.2.2"};
static bool config_available = true;
const z80_network_config_t *z80_network_config() {
  return config_available ? &test_config : nullptr;
}
void sntp_setoperatingmode(unsigned char mode) {
  assert(lock_depth && mode == SNTP_OPMODE_POLL);
}
void sntp_setservername(unsigned char index, const char *name) {
  assert(lock_depth && index == 0 && std::string(name) == "pool.ntp.org");
}
void sntp_init() { assert(lock_depth); ++time_client_starts; }
char *ip4addr_ntoa_r(const struct ip4_addr *, char *buffer, int capacity) {
  assert(lock_depth && capacity >= 10);
  snprintf(buffer, (size_t)capacity, "%s", assigned_address);
  return buffer;
}
void z80_io_ports_set_network_address(const char *address) {
  snprintf(service_address, sizeof(service_address), "%s", address);
}
size_t z80_io_ports_get_network_address(char *address, size_t capacity) {
  int length = snprintf(address, capacity, "%s", service_address);
  return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
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
void cyw43_arch_poll() { ++polls; }
int cyw43_arch_wifi_connect_async(const char *ssid, const char *password, unsigned auth) {
  assert(std::string(ssid) == test_config.ssid);
  assert(std::string(password) == test_config.password);
  assert(auth == (password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN));
  return PICO_OK;
}
int cyw43_tcpip_link_status(cyw43_t *, int) { return fake_link; }
int cyw43_wifi_pm(cyw43_t *, int) { return PICO_OK; }
int cyw43_wifi_leave(cyw43_t *, int) { return PICO_OK; }
absolute_time_t make_timeout_time_ms(uint32_t delay) { return now + delay * 1000ull; }
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
static void advance(uint32_t ms) { now += ms * 1000ull; }

void z80_terminal_browser_connect() {
  assert(lock_depth);
  received.clear();
  outgoing.clear();
  connected = true;
}
void z80_terminal_browser_disconnect() {
  assert(lock_depth);
  if (!connected)
    return;
  connected = false;
  received.clear();
  outgoing.clear();
  ++disconnects;
}
bool z80_terminal_browser_try_receive(const uint8_t *data, size_t length) {
  assert(lock_depth && connected);
  if (received.size() + length > 128)
    return false;
  received.insert(received.end(), data, data + length);
  return true;
}
size_t z80_terminal_supply(uint8_t *data, size_t capacity) {
  assert(lock_depth && connected);
  size_t length = outgoing.size() < capacity ? outgoing.size() : capacity;
  for (size_t i = 0; i < length; ++i)
    data[i] = outgoing[i];
  outgoing.erase(outgoing.begin(), outgoing.begin() + length);
  return length;
}
void WebSocketServer::popMessages() { assert(lock_depth); }
bool WebSocketServer::close(uint32_t connection) {
  assert(lock_depth);
  closed.push_back(connection);
  // The real library begins closing, but does not synchronously call on_close.
  return close_ok;
}
bool WebSocketServer::abort(uint32_t connection) {
  assert(lock_depth);
  closed.push_back(connection);
  return true;
}
WebSocketServer::SendResult WebSocketServer::enqueueMessage(uint32_t connection,
                                                           const void *data, size_t length) {
  return sendMessage(connection, data, length) ? SendResult::queued : SendResult::would_block;
}
bool WebSocketServer::flush(uint32_t) {
  assert(lock_depth);
  ++flushes;
  return flush_ok;
}
bool WebSocketServer::sendMessage(uint32_t connection, const void *data, size_t length) {
  assert(lock_depth && (int32_t)connection == active_connection.load());
  ++sends;
  sent.assign(static_cast<const uint8_t *>(data),
              static_cast<const uint8_t *>(data) + length);
  sent_frames.push_back(sent);
  return send_ok;
}
bool WebSocketServer::sendPing(uint32_t connection, const void *data, size_t length) {
  assert(lock_depth && (int32_t)connection == active_connection.load());
  challenge.assign(static_cast<const uint8_t *>(data),
                   static_cast<const uint8_t *>(data) + length);
  return send_ok;
}
static void connect_client(uint32_t id) {
  cyw43_arch_lwip_begin();
  on_connect(*server, id);
  cyw43_arch_lwip_end();
  z80_web_terminal_poll(true, z80_network_generation());
}
static void close_client(uint32_t id) {
  cyw43_arch_lwip_begin();
  on_close(*server, id);
  cyw43_arch_lwip_end();
}
static void message(uint32_t id, uint8_t type, uint32_t sequence,
                    size_t bytes = 0, uint8_t value = 'N') {
  std::vector<uint8_t> frame(HEADER_BYTES + bytes, value);
  frame[0] = type;
  write_sequence(frame.data() + 1, sequence);
  cyw43_arch_lwip_begin();
  on_message(*server, id, frame.data(), frame.size());
  cyw43_arch_lwip_end();
}
static void pong(uint32_t id) {
  cyw43_arch_lwip_begin();
  on_pong(*server, id, challenge.data(), challenge.size());
  cyw43_arch_lwip_end();
}

static bool poll_network() {
  bool online = z80_network_poll();
  z80_web_terminal_poll(online, z80_network_generation());
  return online;
}

static void poll_terminal() {
  z80_web_terminal_poll(true, z80_network_generation());
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
  advance(5000);
  assert(z80_service_led(false, &led) == IO_SERVICE_OK && !led);
  assert(init_calls == 2 && led_writes == 1 && !physical_led);
  led = true;
  assert(z80_service_led(true, &led) == IO_SERVICE_OK && physical_led);
  led_result = -1;
  led = false;
  assert(z80_service_led(true, &led) == IO_SERVICE_UNAVAILABLE);
  assert(physical_led);
  led_result = 0;
#ifdef TEST_NO_WIFI
  config_available = false;
  assert(!poll_network());
  assert(physical_led && init_calls == 2 && polls == 1);
  puts("PASS: LED control without Wi-Fi credentials");
  return 0;
#endif
  outgoing = {'U'};
  assert(!poll_network());
  assert(outgoing == std::vector<uint8_t>{'U'} && disconnects == 0);
  assert(poll_network());
  assert(time_client_starts == 1 && std::string(service_address) == "192.0.2.1");
  assigned_address = "198.51.100.42";
  unsigned unchanged_polls = polls;
  assert(poll_network() && polls == unchanged_polls + 1);
  assert(std::string(service_address) == "192.0.2.1");
  advance(1000);
  assert(poll_network());
  assert(std::string(service_address) == assigned_address);
  assigned_address = "0.0.0.0";
  advance(1000);
  assert(poll_network() && service_address[0] == '\0');
  assigned_address = "192.0.2.1";
  advance(1000);
  assert(poll_network());
  assert(std::string(service_address) == assigned_address);
  netif_default = nullptr;
  advance(1000);
  assert(poll_network() && service_address[0] == '\0');
  netif_default = &interface;
  advance(1000);
  assert(poll_network());
  assert(std::string(service_address) == assigned_address);
  connect_client(1);
  assert(connected);
  assert(sent[0] == HELLO && read_sequence(sent.data() + 1) == PROTOCOL_VERSION);
  message(1, READY, PROTOCOL_VERSION);
  assert(protocol_ready);
  connect_client(100);
  close_ok = false;
  connect_client(101);
  close_ok = true;
  assert(active_connection.load() == 1 && closed.size() == 2);
  message(100, INPUT, 1, 1);
  assert(received.empty());

  // Retained input is admitted on the next poll, not on a 250 ms retransmit.
  message(1, INPUT, 1, 64, 'A');
  message(1, INPUT, 1, 64, 'A');
  assert(received.size() == 64 && input_sequence == 2 && sent[0] == INPUT_ACK);
  message(1, INPUT, 2, 64, 'B');
  assert(received.size() == 128 && input_sequence == 3);
  unsigned old_sends = sends;
  message(1, INPUT, 3, 64, 'C');
  assert(sends == old_sends && received.size() == 128 && input_sequence == 3);
  assert(pending_input_length == 64);
  message(1, INPUT, 3, 64, 'C');
  assert(pending_input_length == 64 && input_sequence == 3);
  advance(10);
  received.clear();
  send_ok = false;
  poll_terminal();
  assert(received.size() == 64 && input_sequence == 4);
  assert(pending_input_length == 0);
  send_ok = true;
  message(1, INPUT, 3, 64, 'C');
  assert(received.size() == 64 && sent[0] == INPUT_ACK);

  // A failed send must not allow a later sequence to overtake it.
  outgoing.assign(512, 'O');
  send_ok = false;
  poll_terminal();
  assert(output_count == 2 && outgoing.empty() && sent[0] == OUTPUT);
  assert(read_sequence(sent.data() + 1) == 1);
  message(1, OUTPUT_ACK, 2);
  assert(output_count == 2);
  std::vector<uint8_t> first = sent;
  old_sends = sends;
  poll_terminal();
  assert(sends == old_sends + 1);
  advance(FRAME_RETRY_MS);
  poll_terminal();
  assert(sent == first && output_sequence == 3);
  message(1, OUTPUT_ACK, 77);
  assert(output_count == 2);
  message(1, OUTPUT_ACK, 1);
  assert(output_count == 2);
  send_ok = true;
  unsigned old_flushes = flushes;
  flush_ok = false;
  poll_terminal();
  assert(flushes == old_flushes + 1);
  assert(output_count == 2 && output_frames[output_head].sent);
  assert(outgoing.empty() && read_sequence(sent.data() + 1) == 2);
  old_sends = sends;
  poll_terminal();
  assert(sends == old_sends);
  flush_ok = true;
  message(1, OUTPUT_ACK, 1);
  assert(output_count == 1);
  message(1, OUTPUT_ACK, 2);
  assert(output_count == 0 && output_sequence == 3);

  // Four frames can be in flight without any ACK; the fifth remains queued.
  outgoing.assign(512, 'P');
  poll_terminal();
  outgoing.assign(512, 'Q');
  poll_terminal();
  assert(output_count == OUTPUT_WINDOW && output_sequence == 7);
  outgoing.assign(256, 'R');
  old_sends = sends;
  poll_terminal();
  assert(sends == old_sends && outgoing.size() == 256);
  message(1, OUTPUT_ACK, 4);
  assert(output_count == 2);
  poll_terminal();
  assert(output_count == 3 && outgoing.empty());
  message(1, OUTPUT_ACK, 7);
  assert(output_count == 0);

  // Cumulative ACKs and frame ordering also work across uint32 wrap.
  output_sequence = UINT32_MAX;
  outgoing.assign(512, 'W');
  poll_terminal();
  assert(output_count == 2 && read_sequence(sent.data() + 1) == 0);
  message(1, OUTPUT_ACK, 0);
  assert(output_count == 0 && output_sequence == 1);

  // Idle vanished browsers time out even when the Pico Wi-Fi link remains up.
  pong(100);
  advance(CLIENT_TIMEOUT_MS);
  poll_terminal();
  assert(!connected && active_connection.load() == -1 && fake_link == CYW43_LINK_UP);
  connect_client(2);
  message(2, READY, PROTOCOL_VERSION);
  assert(input_sequence == 1 && output_sequence == 1 && output_count == 0);
  poll_terminal();
  pong(2);
  advance(HEARTBEAT_MS);
  poll_terminal();
  pong(2);
  assert(connected);
  // PONG alone cannot keep unrendered terminal output alive forever.
  outgoing = {'X'};
  poll_terminal();
  for (unsigned i = 0; i < 3; ++i) {
    advance(HEARTBEAT_MS);
    poll_terminal();
    if (connected)
      pong(2);
  }
  assert(!connected);

  // A pending frame cannot survive a transport handover.
  connect_client(10);
  message(10, READY, PROTOCOL_VERSION);
  received.assign(128, 'S');
  message(10, INPUT, 1, 64, 'T');
  assert(pending_input_length == 64);
  close_client(10);
  poll_terminal();
  assert(pending_input_length == 0 && !connected);
  connect_client(11);
  message(11, READY, 1);
  assert(!connected && active_connection.load() == -1);

  // Close/reconnect events cannot clear a new client's first input.
  connect_client(3);
  close_client(3);
  cyw43_arch_lwip_begin();
  on_connect(*server, 4);
  cyw43_arch_lwip_end();
  poll_terminal();
  message(4, READY, PROTOCOL_VERSION);
  message(4, INPUT, 1, 1);
  assert(connected && received == std::vector<uint8_t>{'N'});
  close_client(3);
  poll_terminal();
  assert(connected && active_connection.load() == 4);
  fake_link = CYW43_LINK_DOWN;
  assert(!poll_network() && !connected && service_address[0] == '\0');
  advance(RETRY_DELAY_MS);
  assert(!poll_network());
  fake_link = CYW43_LINK_UP;
  assert(poll_network() && time_client_starts == 1);
  outgoing = {'U'};
  received = {'K'};
  unsigned old_disconnects = disconnects;
  test_config.password[0] = '\0';
  assert(!z80_network_poll());
  assert(outgoing == std::vector<uint8_t>{'U'} &&
         received == std::vector<uint8_t>{'K'} && disconnects == old_disconnects);
  z80_web_terminal_poll(false, z80_network_generation());
  assert(service_address[0] == '\0');
  assert(outgoing == std::vector<uint8_t>{'U'} &&
         received == std::vector<uint8_t>{'K'} && disconnects == old_disconnects);
  assert(poll_network());
  fake_link = CYW43_LINK_DOWN;
  assert(!poll_network());
  assert(outgoing == std::vector<uint8_t>{'U'} &&
         received == std::vector<uint8_t>{'K'});
  config_available = false;
  assert(!poll_network());
  assert(!connected && wifi_state == WifiState::uninitialized && lock_depth == 0);
  unsigned offline_sends = sends;
  assert(!poll_network());
  assert(sends == offline_sends && !lock_depth);
  delete server;
  puts("PASS: four-frame window, cumulative/wrapping ACKs, ordered send failure, "
       "batched flush/backpressure, immediate pending input, network lifecycle, heartbeat and USB preservation");
}
