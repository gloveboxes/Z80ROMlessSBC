#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../onboard_led.c"
#include "../../stage10_websocket_terminal/network_runtime.cpp"
static std::string terminal_diagnostics;
static int terminal_printf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  assert(length >= 0 && static_cast<size_t>(length) < sizeof(buffer));
  terminal_diagnostics += buffer;
  return length;
}
#define printf terminal_printf
#include "../../stage10_websocket_terminal/web_terminal.cpp"
#undef printf

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
static int power_mode_result = PICO_OK;
static int power_mode_setting = -1;
static unsigned power_mode_calls;
static bool send_ok = true;
static bool close_ok = true;
static unsigned sends;
static unsigned flushes;
static unsigned queue_snapshot_queries;
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
int cyw43_wifi_pm(cyw43_t *, int mode) {
  power_mode_setting = mode;
  ++power_mode_calls;
  return power_mode_result;
}
int cyw43_wifi_leave(cyw43_t *, int) { return PICO_OK; }
absolute_time_t make_timeout_time_ms(uint32_t delay) { return now + delay * 1000ull; }
absolute_time_t make_timeout_time_us(uint32_t delay) { return now + delay; }
uint64_t time_us_64() { return now; }
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
bool WebSocketServer::getReceiveQueueStats(uint32_t connection, ReceiveQueueStats& stats) {
  ++queue_snapshot_queries;
  if ((int32_t)connection != active_connection.load())
    return false;
  stats = receive_queue_stats;
  stats.messageLimit = receive_limits.queuedMessages;
  stats.byteLimit = receive_limits.queuedBytes;
  return true;
}
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
static void assert_buffer_usage() {
  z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
  size_t bytes = staged_output_length;
  for (size_t i = 0; i < output_count; ++i)
    bytes += output_frames[(output_head + i) % OUTPUT_WINDOW].length - HEADER_BYTES;
  assert(stats.window_frames == output_count && stats.retained_bytes == bytes);
  assert(stats.staged_bytes == staged_output_length);
  assert(stats.window_frames <= stats.window_frames_peak &&
         stats.window_frames_peak <= OUTPUT_WINDOW);
  assert(stats.retained_bytes <= stats.retained_bytes_peak &&
         stats.retained_bytes_peak <= OUTPUT_WINDOW * FRAME_BYTES);
  assert(stats.staged_bytes < FRAME_BYTES);
}

static void connect_client(uint32_t id) {
  cyw43_arch_lwip_begin();
  on_connect(*server, id);
  cyw43_arch_lwip_end();
  z80_web_terminal_poll(true, z80_network_generation());
}
static void close_client(uint32_t id) {
  cyw43_arch_lwip_begin();
  if (server->close_details_callback) {
    const auto& queue = server->receive_queue_stats;
    server->close_details_callback(*server, id, WebSocketServer::CloseReason::peer_tcp_closed,
                                   0, queue.queuedMessages, queue.queuedBytes,
                                   queue.peakQueuedMessages, queue.peakQueuedBytes,
                                   server->receive_limits.queuedMessages,
                                   server->receive_limits.queuedBytes);
  }
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
  assert_buffer_usage();
}
static void pong(uint32_t id) {
  cyw43_arch_lwip_begin();
  on_pong(*server, id, challenge.data(), challenge.size());
  cyw43_arch_lwip_end();
}

static bool poll_network() {
  bool online = z80_network_poll();
  z80_web_terminal_poll(online, z80_network_generation());
  assert_buffer_usage();
  return online;
}

static void poll_terminal() {
  z80_web_terminal_poll(true, z80_network_generation());
  assert_buffer_usage();
}

static void test_output_coalescing() {
  static_assert(FRAME_BYTES == 512 && OUTPUT_WINDOW == 4 && PROTOCOL_VERSION == 4);
  advance(1);
  outgoing = {'A'};
  unsigned before = sends;
  poll_terminal();
  assert(sends == before + 1 && output_count == 1);
  assert(sent == std::vector<uint8_t>({OUTPUT, 1, 0, 0, 0, 'A'}));

  for (unsigned i = 1; i <= 63; ++i) {
    now += 10;
    outgoing.push_back((uint8_t)i);
    poll_terminal();
    assert(sends == before + 1 && output_count == 1);
  }
  now += 369;
  poll_terminal();
  assert(staged_output_length == 63 && sends == before + 1);
  assert(z80_web_terminal_get_stats().retained_bytes == 64);
  assert(z80_web_terminal_get_stats().staged_bytes == 63);
  ++now;
  poll_terminal();
  assert(staged_output_length == 0 && sends == before + 2);
  assert(sent.size() == HEADER_BYTES + 63 && read_sequence(sent.data() + 1) == 2);
  for (unsigned i = 1; i <= 63; ++i)
    assert(sent[HEADER_BYTES + i - 1] == i);

  outgoing.assign(FRAME_BYTES, 'F');
  poll_terminal();
  assert(sends == before + 3 && output_count == 3);
  outgoing = {'E'};
  poll_terminal();
  assert(staged_output_length == 1 && output_count == 3);
  message(1, OUTPUT_ACK, 4);
  assert(staged_output_length == 1 && output_count == 3 && output_sequence == 4);
  message(1, OUTPUT_ACK, 2);
  assert(output_count == 1 && staged_output_length == 1);
  assert(z80_web_terminal_get_stats().retained_bytes == FRAME_BYTES + 1);
  advance(1);
  poll_terminal();
  assert(sent == std::vector<uint8_t>({OUTPUT, 4, 0, 0, 0, 'E'}));
  message(1, OUTPUT_ACK, 4);
  assert(output_count == 0);

  advance(1);
  z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
  sent_frames.clear();
  before = flushes;
  for (unsigned i = 0; i < 256; ++i) {
    outgoing.push_back((uint8_t)i);
    poll_terminal();
    now += 10;
  }
  advance(1);
  poll_terminal();
  assert(output_count == 4 && sent_frames.size() == 4 && flushes == before + 4);
  std::vector<uint8_t> delivered;
  for (const auto &frame : sent_frames)
    delivered.insert(delivered.end(), frame.begin() + HEADER_BYTES, frame.end());
  assert(delivered.size() == 256 && outgoing.empty() && !staged_output_length);
  for (unsigned i = 0; i < 256; ++i)
    assert(delivered[i] == i);
  advance(10);
  message(1, OUTPUT_ACK, output_sequence - 1);
  assert(output_count == 0);
  z80_web_terminal_stats_t after = z80_web_terminal_get_stats();
  assert(after.retransmits == stats.retransmits);
}

static void test_complete_redraw() {
  advance(1);
  sent_frames.clear();
  uint32_t first_sequence = output_sequence;
  unsigned before = sends;
  std::vector<uint8_t> expected(1039);
  for (size_t i = 0; i < expected.size(); ++i)
    expected[i] = (uint8_t)i;
  outgoing.assign(expected.begin(), expected.begin() + 512);
  poll_terminal();
  assert(output_count == 1 && sends == before + 1);
  outgoing.assign(expected.begin() + 512, expected.begin() + 1024);
  poll_terminal();
  assert(output_count == 2 && sends == before + 2 && !staged_output_length);
  outgoing.assign(expected.begin() + 1024, expected.end());
  poll_terminal();
  assert(output_count == 2 && staged_output_length == 15 && outgoing.empty());
  assert(sends == before + 2);
  assert(z80_web_terminal_get_stats().retained_bytes == 1039);
  advance(1);
  poll_terminal();
  assert(output_count == 3 && sends == before + 3 && !staged_output_length);
  assert(sent_frames.size() == 3 && sent_frames[0].size() == HEADER_BYTES + 512 &&
         sent_frames[1].size() == HEADER_BYTES + 512 &&
         sent_frames[2].size() == HEADER_BYTES + 15);
  std::vector<uint8_t> actual;
  for (const auto &frame : sent_frames)
    actual.insert(actual.end(), frame.begin() + HEADER_BYTES, frame.end());
  assert(actual == expected && output_sequence == first_sequence + 3);
  z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
  assert(stats.retained_bytes == 1039 && stats.staged_bytes == 0);
  message(1, OUTPUT_ACK, output_sequence - 1);
  assert(output_count == 0);
}

static void test_input_ack_recovery() {
  connect_client(20);
  message(20, READY, PROTOCOL_VERSION);
  send_ok = false;
  message(20, INPUT, 1, 1, 'K');
  assert(received == std::vector<uint8_t>{'K'} && input_sequence == 2);
  uint64_t recovery_time = now;
  for (unsigned i = 0; i < 3; ++i) {
    unsigned before = sends;
    poll_terminal();
    assert(sends == before + 1 && sent[0] == INPUT_ACK);
    assert(read_sequence(sent.data() + 1) == 1);
    assert(received == std::vector<uint8_t>{'K'} && input_sequence == 2);
  }
  send_ok = true;
  unsigned before = sends;
  poll_terminal();
  assert(now == recovery_time && sends == before + 1);
  assert(sent[0] == INPUT_ACK && read_sequence(sent.data() + 1) == 1);
  poll_terminal();
  assert(sends == before + 1);

  // A rejected ACK for duplicate input needs the same service-pass retry.
  send_ok = false;
  message(20, INPUT, 1, 1, 'K');
  send_ok = true;
  before = sends;
  poll_terminal();
  assert(sends == before + 1 && received == std::vector<uint8_t>{'K'});
  assert(sent[0] == INPUT_ACK && read_sequence(sent.data() + 1) == 1);
  poll_terminal();
  assert(sends == before + 1);

  received.clear();
  input_sequence = UINT32_MAX;
  for (uint32_t sequence : {UINT32_MAX, uint32_t{0}}) {
    send_ok = false;
    message(20, INPUT, sequence, 1, 'W');
    send_ok = true;
    before = sends;
    poll_terminal();
    assert(sends == before + 1 && input_sequence == sequence + 1);
    assert(sent[0] == INPUT_ACK && read_sequence(sent.data() + 1) == sequence);
    poll_terminal();
    assert(sends == before + 1);
  }
  assert(received == std::vector<uint8_t>({'W', 'W'}));

  // Neither connection replacement nor network loss may replay an old ACK.
  send_ok = false;
  message(20, INPUT, 1, 1, 'X');
  close_client(20);
  poll_terminal();
  send_ok = true;
  connect_client(21);
  message(21, READY, PROTOCOL_VERSION);
  before = sends;
  poll_terminal();
  assert(sends == before && received.empty() && input_sequence == 1);
  send_ok = false;
  message(21, INPUT, 1, 1, 'Y');
  z80_web_terminal_poll(false, z80_network_generation());
  send_ok = true;
  connect_client(22);
  message(22, READY, PROTOCOL_VERSION);
  before = sends;
  poll_terminal();
  assert(sends == before && received.empty() && input_sequence == 1);
  close_client(22);
  poll_terminal();
}

static void test_disconnect_diagnostics() {
  using Reason = WebSocketServer::CloseReason;
  assert(!z80_web_terminal_get_stats().rx_queue_active);
  connect_client(30);
  server->receive_queue_stats = {2, 10, 4, 20, 64, 1024};
  unsigned queries = queue_snapshot_queries;
  z80_web_terminal_poll(true, z80_network_generation());
  assert(queue_snapshot_queries == queries);
  auto stats = z80_web_terminal_get_stats();
  assert(stats.rx_queue_active && stats.rx_queue_messages == 2 &&
         stats.rx_queue_bytes == 10 && stats.rx_queue_peak_messages == 4 &&
         stats.rx_queue_peak_bytes == 20 && stats.rx_queue_message_limit == 64 &&
         stats.rx_queue_byte_limit == 1024);
  close_client(30);
  poll_terminal();
  stats = z80_web_terminal_get_stats();
  assert(!stats.rx_queue_active && stats.rx_queue_messages == 0 &&
         stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_PEER_TCP_CLOSED);

  connect_client(31);
  cyw43_arch_lwip_begin();
  disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR);
  cyw43_arch_lwip_end();
  stats = z80_web_terminal_get_stats();
  assert(stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_UNKNOWN &&
         stats.last_ws_transport_error == 0 && stats.last_ws_queue_peak_messages == 0);

  // Teardown can arrive after a replacement connects; preserve that session's queue.
  connect_client(32);
  server->receive_queue_stats = {1, 5, 2, 10, 64, 1024};
  stats = z80_web_terminal_get_stats();
  assert(stats.rx_queue_active && stats.rx_queue_peak_messages == 2);
  auto finish_close = [](uint32_t id, Reason reason, int error) {
    cyw43_arch_lwip_begin();
    server->close_details_callback(*server, id, reason, error,
                                   3, 15, 9, 45, 64, 1024);
    on_close(*server, id);
    cyw43_arch_lwip_end();
  };
  finish_close(31, Reason::tcp_write_error, -16);
  assert(connected && active_connection.load() == 32 &&
         terminal_stats.rx_queue_messages == 1 &&
         terminal_stats.rx_queue_peak_messages == 2);
  stats = z80_web_terminal_get_stats();
  assert(stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR &&
         stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_TCP_WRITE_ERROR &&
         stats.last_ws_transport_error == -16 && stats.last_ws_queue_peak_messages == 9);

  cyw43_arch_lwip_begin();
  disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
  cyw43_arch_lwip_end();
  finish_close(31, Reason::tcp_write_error, -16);
  stats = z80_web_terminal_get_stats();
  assert(stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_UNKNOWN);
  finish_close(32, Reason::server_abort, 0);
  stats = z80_web_terminal_get_stats();
  assert(!stats.rx_queue_active && stats.rx_queue_messages == 0 &&
         stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR &&
         stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_SERVER_ABORT &&
         stats.last_ws_queue_peak_messages == 9);

  connect_client(33);
  close_client(33);
  poll_terminal();
  finish_close(32, Reason::tcp_write_error, -16);
  stats = z80_web_terminal_get_stats();
  assert(stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED &&
         stats.last_ws_close_reason == Z80_WEB_TERMINAL_WS_CLOSE_PEER_TCP_CLOSED &&
         stats.last_ws_transport_error == 0);
  server->receive_queue_stats = {};
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
  power_mode_result = -1;
  assert(!poll_network());
  assert(power_mode_calls == 1 &&
         power_mode_setting == CYW43_NO_POWERSAVE_MODE &&
         wifi_state == WifiState::association_backoff);
  power_mode_result = PICO_OK;
  advance(RETRY_DELAY_MS);
  assert(!poll_network());
  assert(poll_network());
  assert(power_mode_calls == 2 &&
         power_mode_setting == CYW43_NO_POWERSAVE_MODE);
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
  old_sends = sends;
  uint64_t recovery_time = now;
  poll_terminal();
  assert(now == recovery_time && sends == old_sends + 1);
  assert(received.size() == 64 && sent[0] == INPUT_ACK);
  assert(read_sequence(sent.data() + 1) == 3);
  poll_terminal();
  assert(sends == old_sends + 1);
  message(1, INPUT, 3, 64, 'C');
  assert(received.size() == 64 && sent[0] == INPUT_ACK);

  // A failed send must not allow a later sequence to overtake it.
  outgoing.assign(2 * FRAME_BYTES, 'O');
  send_ok = false;
  z80_web_terminal_stats_t old_stats = z80_web_terminal_get_stats();
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
  assert(z80_web_terminal_get_stats().retained_bytes == 2 * FRAME_BYTES);
  message(1, OUTPUT_ACK, 77);
  assert(output_count == 2);
  message(1, OUTPUT_ACK, 1);
  assert(output_count == 2);
  send_ok = true;
  unsigned old_flushes = flushes;
  flush_ok = false;
  poll_terminal();
  assert(flushes == old_flushes + 1);
  assert(z80_web_terminal_get_stats().flush_failures == old_stats.flush_failures + 1);
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
  outgoing.assign(2 * FRAME_BYTES, 'P');
  poll_terminal();
  outgoing.assign(2 * FRAME_BYTES, 'Q');
  poll_terminal();
  assert(output_count == OUTPUT_WINDOW && output_sequence == 7);
  assert(z80_web_terminal_get_stats().retained_bytes == 2048);
  assert(z80_web_terminal_get_stats().retained_bytes_peak == 2048);
  assert(z80_web_terminal_get_stats().window_frames_peak == 4);
  outgoing.assign(FRAME_BYTES, 'R');
  old_sends = sends;
  poll_terminal();
  assert(sends == old_sends && outgoing.size() == FRAME_BYTES);
  message(1, OUTPUT_ACK, 4);
  assert(output_count == 2);
  poll_terminal();
  assert(output_count == 3 && outgoing.empty());
  message(1, OUTPUT_ACK, 7);
  assert(output_count == 0);

  // Cumulative ACKs and frame ordering also work across uint32 wrap.
  output_sequence = UINT32_MAX;
  outgoing.assign(2 * FRAME_BYTES, 'W');
  poll_terminal();
  assert(output_count == 2 && read_sequence(sent.data() + 1) == 0);
  message(1, OUTPUT_ACK, 0);
  assert(output_count == 0 && output_sequence == 1);
  test_output_coalescing();
  test_complete_redraw();
  old_stats = z80_web_terminal_get_stats();
  outgoing.assign(FRAME_BYTES, 'T');
  poll_terminal();
  advance(FRAME_RETRY_MS);
  poll_terminal();
  assert(z80_web_terminal_get_stats().retransmits == old_stats.retransmits + 1);
  message(1, OUTPUT_ACK, output_sequence - 1);

  // Idle PINGs are 30 seconds apart; disconnect after three missed PONGs.
  advance(HEARTBEAT_MS);
  poll_terminal();
  for (unsigned miss = 0; miss <= MAX_MISSED_PONGS; ++miss) {
    advance(HEARTBEAT_MS);
    poll_terminal();
    if (miss < MAX_MISSED_PONGS)
      assert(connected);
  }
  assert(!connected && active_connection.load() == -1 && fake_link == CYW43_LINK_UP);
  assert(z80_web_terminal_get_stats().pong_timeouts >= 1);
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
  for (unsigned i = 0; i <= OUTPUT_ACK_TIMEOUT_MS / HEARTBEAT_MS; ++i) {
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
  outgoing = {'A'};
  poll_terminal();
  outgoing = {'B'};
  poll_terminal();
  assert(staged_output_length == 1);
  close_client(10);
  poll_terminal();
  assert(pending_input_length == 0 && staged_output_length == 0 && !connected);
  assert(z80_web_terminal_get_stats().retained_bytes == 0);
  assert(z80_web_terminal_get_stats().window_frames == 0);
  assert(z80_web_terminal_get_stats().retained_bytes_peak == 2048);
  connect_client(11);
  message(11, READY, 2);
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
  uint32_t before_fs_change = z80_network_generation();
  strcpy(test_config.fs_server, "192.0.2.99");
  assert(poll_network() && z80_network_generation() == before_fs_change);
  assert(wifi_state == WifiState::online);
  test_config.fs_server[0] = '\0';
  assert(poll_network() && z80_network_generation() == before_fs_change);
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
  test_input_ack_recovery();
  test_disconnect_diagnostics();
  connect_client(90);
  // Controls from another socket or before READY cannot request a reboot.
  message(91, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_IDLE);
  message(90, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_IDLE);
  connect_client(92);
  message(92, READY, PROTOCOL_VERSION);
  received.assign(128, 'Q');
  message(92, INPUT, 1, INPUT_BYTES);
  assert(pending_input_length == INPUT_BYTES);
  message(92, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_REQUESTED);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_CPM_READY);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_REQUESTED);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_FLUSHING);
  message(92, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_FLUSHING);
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS && read_sequence(sent.data() + 1) == Z80_WEB_REBOOT_FLUSHING);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_STORAGE_ERROR);
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS && read_sequence(sent.data() + 1) == Z80_WEB_REBOOT_STORAGE_ERROR);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_RESTARTING);
  unsigned close_count = closed.size();
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS && read_sequence(sent.data() + 1) == Z80_WEB_REBOOT_RESTARTING);
  assert(closed.size() == close_count + 1 && closed.back() == 92);
  poll_terminal();
  assert(closed.size() == close_count + 1); // One close per status transition.
  close_client(92);
  assert(terminal_diagnostics.find("[network] Terminal: http://") ==
         std::string::npos);
  delete server;
  puts("PASS: four-frame window, cumulative/wrapping ACKs, ordered send failure, "
       "batched flush/backpressure, immediate pending input and ACK recovery, "
       "network lifecycle, heartbeat and USB preservation");
}
