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
static unsigned address_formats;
static unsigned address_publishes;
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
static bool enqueue_fails;
// Set while a transport callback runs: callbacks must not transmit.
static bool in_callback;
static unsigned clock_reads;
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
size_t z80_service_info(uint8_t selector, char *text, size_t capacity) {
  assert(selector == 0 && capacity >= sizeof("z80-nexus-123456"));
  snprintf(text, capacity, "z80-nexus-123456");
  return strlen(text);
}
void sntp_setoperatingmode(unsigned char mode) {
  assert(lock_depth && mode == SNTP_OPMODE_POLL);
}
void sntp_setservername(unsigned char index, const char *name) {
  assert(lock_depth && index == 0 && std::string(name) == "pool.ntp.org");
}
void sntp_init() { assert(lock_depth); ++time_client_starts; }
// Like lwIP, the binary address holds the octets in memory (network) order.
static uint32_t ipv4_from_text(const char *text) {
  unsigned octets[4];
  assert(sscanf(text, "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2], &octets[3]) == 4);
  uint8_t bytes[4] = {(uint8_t)octets[0], (uint8_t)octets[1],
                      (uint8_t)octets[2], (uint8_t)octets[3]};
  uint32_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}
static void assign_address(struct netif &interface, const char *text) {
  assigned_address = text;
  interface.address.address = ipv4_from_text(text);
}
char *ip4addr_ntoa_r(const struct ip4_addr *address, char *buffer, int capacity) {
  assert(lock_depth && capacity >= 16);
  ++address_formats;
  uint8_t bytes[4];
  memcpy(bytes, &address->address, sizeof(bytes));
  snprintf(buffer, (size_t)capacity, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
  return buffer;
}
void netif_set_hostname(struct netif *interface, const char *hostname) {
  assert(lock_depth && interface && std::string(hostname) == "z80-nexus-123456");
  interface->hostname = hostname;
}
void z80_io_ports_set_network_address(const char *address) {
  ++address_publishes;
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
uint64_t time_us_64() { ++clock_reads; return now; }
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
void WebSocketServer::poll() { assert(lock_depth); }
bool WebSocketServer::close(uint32_t connection) {
  assert(lock_depth && !in_callback);
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
  assert(!in_callback);
  if (enqueue_fails)
    return SendResult::error;
  return sendMessage(connection, data, length) ? SendResult::queued : SendResult::would_block;
}
bool WebSocketServer::flush(uint32_t) {
  assert(lock_depth && !in_callback);
  ++flushes;
  return flush_ok;
}
bool WebSocketServer::sendMessage(uint32_t connection, const void *data, size_t length) {
  assert(lock_depth && !in_callback && (int32_t)connection == active_connection);
  ++sends;
  sent.assign(static_cast<const uint8_t *>(data),
              static_cast<const uint8_t *>(data) + length);
  if (send_ok)
    sent_frames.push_back(sent);
  return send_ok;
}
bool WebSocketServer::sendPing(uint32_t connection, const void *data, size_t length) {
  assert(lock_depth && !in_callback && (int32_t)connection == active_connection);
  challenge.assign(static_cast<const uint8_t *>(data),
                   static_cast<const uint8_t *>(data) + length);
  return send_ok;
}

using Reason = WebSocketServer::CloseReason;
static uint32_t sent_sequence() { return read_sequence(sent.data() + 1); }
static void assert_output_usage() {
  z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
  assert(stats.window_frames == frames_in_flight() && stats.window_frames <= OUTPUT_WINDOW);
  assert(stats.window_frames <= stats.window_frames_peak &&
         stats.window_frames_peak <= OUTPUT_WINDOW);
  assert(stats.staged_bytes == staged_length && staged_length <= FRAME_BYTES);
}

// The transport calls back on core 1 from inside the radio's receive path,
// where the terminal may record state but never transmit.
template <typename Callback> static void in_transport_callback(Callback callback) {
  cyw43_arch_lwip_begin();
  in_callback = true;
  callback();
  in_callback = false;
  cyw43_arch_lwip_end();
}
static void connect_client(uint32_t id) {
  in_transport_callback([id] { on_connect(*server, id); });
}
static void close_client(uint32_t id, Reason reason = Reason::peer_tcp_closed,
                         int transport_error = 0) {
  in_transport_callback([=] { on_close(*server, id, reason, transport_error); });
}
static void deliver(uint32_t id, const std::vector<uint8_t> &frame) {
  in_transport_callback([&] { on_message(*server, id, frame.data(), frame.size()); });
  assert_output_usage();
}
static void message(uint32_t id, uint8_t type, uint32_t sequence,
                    size_t bytes = 0, uint8_t value = 'N') {
  std::vector<uint8_t> frame(HEADER_BYTES + bytes, value);
  frame[0] = type;
  write_sequence(frame.data() + 1, sequence);
  deliver(id, frame);
}
static void pong(uint32_t id) {
  in_transport_callback([id] { on_pong(*server, id, challenge.data(), challenge.size()); });
}

static void poll_terminal();
// Connect and complete the handshake; the service pass sends each control.
static void ready_client(uint32_t id) {
  connect_client(id);
  poll_terminal();
  assert(sent[0] == HELLO);
  message(id, READY, PROTOCOL_VERSION);
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS);
  assert(connected && protocol_ready && active_connection == (int32_t)id);
}

static bool poll_network() {
  bool online = z80_network_poll();
  z80_web_terminal_poll(online, z80_network_generation());
  assert_output_usage();
  return online;
}

static void poll_terminal() {
  z80_web_terminal_poll(true, z80_network_generation());
  assert_output_usage();
}

static void test_handshake() {
  // The service pass sends HELLO once; nothing repeats it.
  unsigned before = sends;
  connect_client(1);
  assert(connected && !protocol_ready && hello_pending && sends == before);
  poll_terminal();
  assert(!hello_pending && sends == before + 1);
  assert(sent == std::vector<uint8_t>({HELLO, PROTOCOL_VERSION, 0, 0, 0}));
  outgoing = {'h'};
  for (unsigned i = 0; i < 3; ++i) {
    advance(250);
    poll_terminal();
  }
  // Output waits in the console queue until the browser is ready.
  assert(sends == before + 1 && outgoing.size() == 1 && !staged_length);
  // An idle connected pass reads the clock once; no connection, not at all.
  unsigned reads = clock_reads;
  poll_terminal();
  assert(clock_reads == reads + 1);
  message(1, READY, PROTOCOL_VERSION);
  assert(protocol_ready && sends == before + 1);
  message(1, READY, PROTOCOL_VERSION);
  assert(protocol_ready && connected);
  sent_frames.clear();
  poll_terminal();
  assert(sent_frames.size() == 2 && sent_frames[0][0] == REBOOT_STATUS);
  assert(sent == std::vector<uint8_t>({OUTPUT, 1, 0, 0, 0, 'h'}));
  message(1, OUTPUT_ACK, 1);
  close_client(1);
  assert(!connected && active_connection == -1);
  reads = clock_reads;
  before = sends;
  poll_terminal();
  assert(clock_reads == reads && sends == before);

  // A HELLO that TCP could not take is offered again on the next pass.
  send_ok = false;
  connect_client(2);
  poll_terminal();
  assert(hello_pending);
  send_ok = true;
  before = sends;
  poll_terminal();
  assert(!hello_pending && sends == before + 1 && sent[0] == HELLO);
  poll_terminal();
  assert(sends == before + 1);
  close_client(2);
}

static void test_last_connection_wins() {
  message(1, INPUT, 1, INPUT_BYTES, 'A');
  poll_terminal();
  message(1, INPUT, 2, INPUT_BYTES, 'B');
  poll_terminal();
  message(1, INPUT, 3, INPUT_BYTES, 'C');
  assert(received.size() == 128 && pending_input_length == INPUT_BYTES);
  outgoing.assign(FRAME_BYTES, 'O');
  poll_terminal();
  assert(frames_in_flight() == 1);
  awaiting_pong = true;
  missed_pongs = 2;

  unsigned closes = closed.size();
  unsigned old_disconnects = disconnects;
  connect_client(100);
  assert(active_connection == 100 && connected);
  assert(closed.size() == closes + 1 && closed.back() == 1);
  assert(disconnects == old_disconnects);
  assert(!protocol_ready && !awaiting_pong && missed_pongs == 0);
  assert(received.empty() && outgoing.empty() && pending_input_length == 0);
  assert(frames_in_flight() == 0 && staged_length == 0);
  assert(input_sequence == 1 && output_sequence == 1);
  poll_terminal();
  assert(sent[0] == HELLO && sent_sequence() == PROTOCOL_VERSION);
  // The replaced session can no longer reach the terminal.
  message(1, READY, PROTOCOL_VERSION);
  message(1, INPUT, 1, 1);
  assert(!protocol_ready && received.empty());
  message(100, READY, PROTOCOL_VERSION);
  message(100, INPUT, 1, 1, 'N');
  close_client(1, Reason::server_abort);
  poll_terminal();
  assert(active_connection == 100 && connected && protocol_ready);
  assert(received == std::vector<uint8_t>{'N'} && input_sequence == 2);

  // Several upgrades in one network poll must still select only the last.
  cyw43_arch_lwip_begin();
  on_connect(*server, 101);
  on_connect(*server, 102);
  on_close(*server, 100, Reason::server_abort, 0);
  on_close(*server, 101, Reason::server_abort, 0);
  cyw43_arch_lwip_end();
  poll_terminal();
  assert(active_connection == 102 && connected && !protocol_ready);
  assert(received.empty() && closed[closes + 1] == 100 && closed.back() == 101);
  message(102, READY, PROTOCOL_VERSION);
  message(102, INPUT, 1, 1, 'L');
  assert(received == std::vector<uint8_t>{'L'});
  close_client(102);
  assert(!connected && active_connection == -1);

  ready_client(1);
}

// One frame at a time: the ACK means queued for the Z80, and a frame that
// does not fit waits for room instead of evicting older input.
static void test_input_flow_control() {
  received.clear();
  unsigned old_sends = sends;
  message(1, INPUT, 1, 64, 'A');
  // The bytes are queued at once; the ACK goes out with the service pass.
  assert(received.size() == 64 && input_sequence == 2 && sends == old_sends);
  poll_terminal();
  assert(sends == old_sends + 1 && sent[0] == INPUT_ACK && sent_sequence() == 1);
  message(1, INPUT, 2, 64, 'B');
  assert(received.size() == 128 && input_sequence == 3);
  poll_terminal();
  old_sends = sends;
  message(1, INPUT, 3, 64, 'C');
  assert(sends == old_sends && received.size() == 128 && input_sequence == 3);
  assert(pending_input_length == 64);
  poll_terminal();
  assert(sends == old_sends && pending_input_length == 64);
  // Admitted on the first pass with room; a blocked ACK is offered again.
  received.clear();
  send_ok = false;
  poll_terminal();
  assert(received == std::vector<uint8_t>(64, 'C') && input_sequence == 4);
  assert(pending_input_length == 0);
  send_ok = true;
  old_sends = sends;
  uint64_t recovery_time = now;
  poll_terminal();
  assert(now == recovery_time && sends == old_sends + 1);
  assert(sent[0] == INPUT_ACK && sent_sequence() == 3);
  poll_terminal();
  assert(sends == old_sends + 1 && received.size() == 64);
  received.clear();
}

static void test_output_window() {
  // TCP backpressure: the frame stays staged, its sequence is not consumed
  // and later bytes cannot overtake it.
  std::vector<uint8_t> expected(2 * FRAME_BYTES);
  for (size_t i = 0; i < expected.size(); ++i)
    expected[i] = (uint8_t)(i * 7);
  outgoing = expected;
  send_ok = false;
  z80_web_terminal_stats_t old_stats = z80_web_terminal_get_stats();
  poll_terminal();
  assert(frames_in_flight() == 0 && output_sequence == 1);
  assert(staged_length == FRAME_BYTES && outgoing.size() == FRAME_BYTES);
  assert(sent[0] == OUTPUT && sent_sequence() == 1);
  std::vector<uint8_t> first = sent;
  unsigned old_sends = sends;
  poll_terminal();
  assert(sends == old_sends + 1 && sent == first && output_sequence == 1);
  send_ok = true;
  sent_frames.clear();
  unsigned old_flushes = flushes;
  flush_ok = false;
  poll_terminal();
  assert(frames_in_flight() == 2 && output_sequence == 3 && outgoing.empty());
  // One flush for the burst; its failure does not offer the frames again.
  assert(flushes == old_flushes + 1);
  assert(z80_web_terminal_get_stats().flush_failures == old_stats.flush_failures + 1);
  assert(sent_frames.size() == 2 && sent_frames[0] == first);
  std::vector<uint8_t> delivered;
  for (const auto &frame : sent_frames)
    delivered.insert(delivered.end(), frame.begin() + HEADER_BYTES, frame.end());
  assert(delivered == expected);
  flush_ok = true;
  // TCP owns accepted frames: no timer ever sends one again.
  old_sends = sends;
  for (unsigned i = 0; i < 8; ++i) {
    advance(250);
    poll_terminal();
  }
  assert(sends == old_sends && frames_in_flight() == 2);
  message(1, OUTPUT_ACK, 1);
  assert(frames_in_flight() == 1);
  message(1, OUTPUT_ACK, 2);
  assert(frames_in_flight() == 0 && output_sequence == 3);

  // Four frames may be in flight; a fifth waits staged, the rest stay queued.
  outgoing.assign(4 * FRAME_BYTES, 'P');
  old_flushes = flushes;
  poll_terminal();
  assert(frames_in_flight() == OUTPUT_WINDOW && output_sequence == 7);
  assert(flushes == old_flushes + 1);
  assert(z80_web_terminal_get_stats().window_frames_peak == OUTPUT_WINDOW);
  outgoing.assign(FRAME_BYTES + 10, 'R');
  old_sends = sends;
  poll_terminal();
  assert(sends == old_sends && staged_length == FRAME_BYTES && outgoing.size() == 10);
  assert(z80_web_terminal_get_stats().staged_bytes == FRAME_BYTES);
  // A cumulative ACK frees the window; the next service pass refills it.
  old_flushes = flushes;
  message(1, OUTPUT_ACK, 4);
  assert(sends == old_sends && frames_in_flight() == 2);
  poll_terminal();
  assert(sends == old_sends + 1 && flushes == old_flushes + 1);
  assert(sent.size() == HEADER_BYTES + FRAME_BYTES && sent_sequence() == 7);
  assert(frames_in_flight() == 3 && staged_length == 10 && outgoing.empty());
  advance(1);
  poll_terminal();
  assert(sent.size() == HEADER_BYTES + 10 && sent_sequence() == 8);
  assert(frames_in_flight() == OUTPUT_WINDOW);
  message(1, OUTPUT_ACK, 8);
  assert(frames_in_flight() == 0);

  // Cumulative ACKs and frame ordering also work across uint32 wrap.
  output_sequence = UINT32_MAX;
  output_acked = UINT32_MAX - 1;
  outgoing.assign(2 * FRAME_BYTES, 'W');
  advance(1);
  sent_frames.clear();
  poll_terminal();
  assert(frames_in_flight() == 2 && output_sequence == 1);
  assert(read_sequence(sent_frames[0].data() + 1) == UINT32_MAX && sent_sequence() == 0);
  message(1, OUTPUT_ACK, 0);
  assert(frames_in_flight() == 0 && output_sequence == 1 && connected);
  output_acked = 0;
}

static void test_output_coalescing() {
  static_assert(FRAME_BYTES == 512 && OUTPUT_WINDOW == 4 && PROTOCOL_VERSION == 5);
  advance(1);
  outgoing = {'A'};
  unsigned before = sends;
  poll_terminal();
  assert(sends == before + 1 && frames_in_flight() == 1);
  assert(sent == std::vector<uint8_t>({OUTPUT, 1, 0, 0, 0, 'A'}));

  for (unsigned i = 1; i <= 63; ++i) {
    now += 10;
    outgoing.push_back((uint8_t)i);
    poll_terminal();
    assert(sends == before + 1 && frames_in_flight() == 1);
  }
  now += 369;
  poll_terminal();
  assert(staged_length == 63 && sends == before + 1);
  assert(z80_web_terminal_get_stats().staged_bytes == 63);
  ++now;
  poll_terminal();
  assert(staged_length == 0 && sends == before + 2);
  assert(sent.size() == HEADER_BYTES + 63 && sent_sequence() == 2);
  for (unsigned i = 1; i <= 63; ++i)
    assert(sent[HEADER_BYTES + i - 1] == i);
  message(1, OUTPUT_ACK, 2);

  advance(1);
  sent_frames.clear();
  before = flushes;
  for (unsigned i = 0; i < 256; ++i) {
    outgoing.push_back((uint8_t)i);
    poll_terminal();
    now += 10;
  }
  advance(1);
  poll_terminal();
  assert(frames_in_flight() == 4 && sent_frames.size() == 4 && flushes == before + 4);
  std::vector<uint8_t> delivered;
  for (const auto &frame : sent_frames)
    delivered.insert(delivered.end(), frame.begin() + HEADER_BYTES, frame.end());
  assert(delivered.size() == 256 && outgoing.empty() && !staged_length);
  for (unsigned i = 0; i < 256; ++i)
    assert(delivered[i] == i);
  message(1, OUTPUT_ACK, output_sequence - 1);
  assert(frames_in_flight() == 0);
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
  assert(frames_in_flight() == 1 && sends == before + 1);
  outgoing.assign(expected.begin() + 512, expected.begin() + 1024);
  poll_terminal();
  assert(frames_in_flight() == 2 && sends == before + 2 && !staged_length);
  outgoing.assign(expected.begin() + 1024, expected.end());
  poll_terminal();
  assert(frames_in_flight() == 2 && staged_length == 15 && outgoing.empty());
  assert(sends == before + 2);
  advance(1);
  poll_terminal();
  assert(frames_in_flight() == 3 && sends == before + 3 && !staged_length);
  assert(sent_frames.size() == 3 && sent_frames[0].size() == HEADER_BYTES + 512 &&
         sent_frames[1].size() == HEADER_BYTES + 512 &&
         sent_frames[2].size() == HEADER_BYTES + 15);
  std::vector<uint8_t> actual;
  for (const auto &frame : sent_frames)
    actual.insert(actual.end(), frame.begin() + HEADER_BYTES, frame.end());
  assert(actual == expected && output_sequence == first_sequence + 3);
  message(1, OUTPUT_ACK, output_sequence - 1);
  assert(frames_in_flight() == 0);
}

static void test_heartbeat() {
  // Idle PINGs are 30 seconds apart; disconnect after three missed PONGs.
  const uint32_t heartbeat_ms = HEARTBEAT_US / 1000;
  z80_web_terminal_stats_t old_stats = z80_web_terminal_get_stats();
  advance(heartbeat_ms);
  poll_terminal();
  assert(awaiting_pong && challenge.size() == 4);
  for (unsigned miss = 0; miss <= MAX_MISSED_PONGS; ++miss) {
    advance(heartbeat_ms);
    poll_terminal();
    if (miss < MAX_MISSED_PONGS)
      assert(connected);
  }
  assert(!connected && active_connection == -1 && fake_link == CYW43_LINK_UP);
  z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
  assert(stats.pong_timeouts == old_stats.pong_timeouts + MAX_MISSED_PONGS + 1);
  assert(stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_PONG_TIMEOUT);

  ready_client(2);
  assert(input_sequence == 1 && output_sequence == 1 && frames_in_flight() == 0);
  poll_terminal();
  pong(2);
  advance(heartbeat_ms);
  poll_terminal();
  pong(2);
  assert(connected && !awaiting_pong);
  // Any frame from the browser proves it is alive, even without the PONG.
  for (unsigned i = 0; i < 2 * (MAX_MISSED_PONGS + 1u); ++i) {
    advance(heartbeat_ms);
    poll_terminal();
    message(2, INPUT, input_sequence, 1, 'k');
    received.clear();
  }
  assert(connected);
  pong(2);
  poll_terminal();
  // PONG alone cannot keep unrendered terminal output alive forever.
  old_stats = z80_web_terminal_get_stats();
  outgoing = {'X'};
  poll_terminal();
  assert(frames_in_flight() == 1);
  for (unsigned i = 0; i <= OUTPUT_ACK_TIMEOUT_US / HEARTBEAT_US; ++i) {
    advance(heartbeat_ms);
    poll_terminal();
    if (connected)
      pong(2);
  }
  assert(!connected);
  stats = z80_web_terminal_get_stats();
  assert(stats.ack_timeouts == old_stats.ack_timeouts + 1 &&
         stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_ACK_TIMEOUT);
}

static void test_handover() {
  // Pending input and staged output cannot survive a transport handover.
  ready_client(10);
  received.assign(128, 'S');
  message(10, INPUT, 1, 64, 'T');
  assert(pending_input_length == 64);
  outgoing = {'A'};
  poll_terminal();
  outgoing = {'B'};
  poll_terminal();
  assert(staged_length == 1 && frames_in_flight() == 1);
  close_client(10);
  assert(pending_input_length == 0 && staged_length == 0 && !connected);
  assert(z80_web_terminal_get_stats().window_frames == 0);
  assert(z80_web_terminal_get_stats().staged_bytes == 0);

  // Close/reconnect events cannot clear a new client's first input.
  connect_client(3);
  close_client(3);
  ready_client(4);
  message(4, INPUT, 1, 1);
  assert(connected && received == std::vector<uint8_t>{'N'});
  close_client(3);
  poll_terminal();
  assert(connected && active_connection == 4);
}

// Protocol 5 has no retransmission, so any frame out of step is an error.
static void test_protocol_errors() {
  uint32_t id = 200;
  auto rejected = [&id](const char *problem) {
    z80_web_terminal_stats_t stats = z80_web_terminal_get_stats();
    assert(!connected && active_connection == -1 && closed.back() == id);
    assert(stats.last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
    assert(terminal_diagnostics.find(problem) != std::string::npos);
    terminal_diagnostics.clear();
    received.clear();
    outgoing.clear();
    ++id;
  };
  terminal_diagnostics.clear();
  connect_client(id);
  deliver(id, {READY, 5, 0, 0});
  rejected("invalid terminal protocol");
  for (uint32_t version : {1u, 2u, 3u, 4u, 6u}) {
    connect_client(id);
    message(id, READY, version);
    rejected("before the handshake");
  }
  connect_client(id);
  message(id, INPUT, 1, 1);
  rejected("before the handshake");

  ready_client(id);
  message(id, INPUT, 1, 1);
  message(id, INPUT, 1, 1);
  rejected("out-of-order terminal input");
  ready_client(id);
  message(id, INPUT, 2, 1);
  rejected("out-of-order terminal input");
  // The browser may not send another frame before it has the ACK.
  ready_client(id);
  message(id, INPUT, 1, 1);
  message(id, INPUT, 2, 1);
  rejected("out-of-order terminal input");
  ready_client(id);
  received.assign(128, 'F');
  message(id, INPUT, 1, 1);
  assert(connected && pending_input_length == 1);
  message(id, INPUT, 2, 1);
  rejected("out-of-order terminal input");
  // The frame waiting for room is the only one the browser may have sent.
  ready_client(id);
  received.assign(128, 'F');
  message(id, INPUT, 1, 1);
  message(id, INPUT, 1, 1);
  rejected("out-of-order terminal input");
  ready_client(id);
  message(id, INPUT, 1, 0);
  rejected("invalid terminal frame");
  ready_client(id);
  message(id, INPUT, 1, INPUT_BYTES + 1);
  rejected("invalid terminal frame");

  ready_client(id);
  message(id, OUTPUT_ACK, 1);
  rejected("unexpected terminal output acknowledgement");
  ready_client(id);
  outgoing = {'x'};
  poll_terminal();
  message(id, OUTPUT_ACK, 2);
  rejected("unexpected terminal output acknowledgement");
  ready_client(id);
  outgoing = {'x'};
  poll_terminal();
  message(id, OUTPUT_ACK, 1);
  assert(connected);
  message(id, OUTPUT_ACK, 1);
  rejected("unexpected terminal output acknowledgement");
  ready_client(id);
  message(id, OUTPUT_ACK, 1, 1);
  rejected("invalid terminal frame");
  ready_client(id);
  message(id, 9, 0);
  rejected("invalid terminal frame");
  ready_client(id);
  message(id, REBOOT, 1);
  rejected("invalid terminal frame");
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_IDLE);

  // A transport that rejects output releases the terminal to USB.
  ready_client(id);
  outgoing = {'x'};
  enqueue_fails = true;
  poll_terminal();
  enqueue_fails = false;
  assert(!connected && closed.back() == id);
  assert(z80_web_terminal_get_stats().last_disconnect_reason ==
         Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR);
  outgoing.clear();
  terminal_diagnostics.clear();
}

static void test_input_ack_recovery() {
  ready_client(20);
  send_ok = false;
  message(20, INPUT, 1, 1, 'K');
  assert(received == std::vector<uint8_t>{'K'} && input_sequence == 2);
  uint64_t recovery_time = now;
  for (unsigned i = 0; i < 3; ++i) {
    unsigned before = sends;
    poll_terminal();
    assert(sends == before + 1 && sent[0] == INPUT_ACK);
    assert(sent_sequence() == 1);
    assert(received == std::vector<uint8_t>{'K'} && input_sequence == 2);
  }
  send_ok = true;
  unsigned before = sends;
  poll_terminal();
  assert(now == recovery_time && sends == before + 1);
  assert(sent[0] == INPUT_ACK && sent_sequence() == 1);
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
    assert(sent[0] == INPUT_ACK && sent_sequence() == sequence);
    poll_terminal();
    assert(sends == before + 1);
  }
  assert(received == std::vector<uint8_t>({'W', 'W'}));

  // Neither connection replacement nor network loss may replay an old ACK.
  send_ok = false;
  message(20, INPUT, 1, 1, 'X');
  close_client(20);
  send_ok = true;
  ready_client(21);
  before = sends;
  poll_terminal();
  assert(sends == before && received.empty() && input_sequence == 1);
  send_ok = false;
  message(21, INPUT, 1, 1, 'Y');
  z80_web_terminal_poll(false, z80_network_generation());
  send_ok = true;
  ready_client(22);
  before = sends;
  poll_terminal();
  assert(sends == before && received.empty() && input_sequence == 1);
  close_client(22);
  poll_terminal();
}

static void test_disconnect_diagnostics() {
  auto stats = z80_web_terminal_get_stats;
  terminal_diagnostics.clear();
  connect_client(30);
  close_client(30);
  assert(stats().last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED &&
         stats().last_ws_close_reason == (uint32_t)Reason::peer_tcp_closed);
  assert(terminal_diagnostics ==
         "[network] Terminal connection closed: peer-tcp-closed; restoring USB.\n");
  connect_client(31);
  close_client(31, Reason::tcp_error, -14);
  assert(terminal_diagnostics.find("closed: tcp-error (lwIP error=-14); restoring USB.") !=
         std::string::npos);
  assert(stats().last_ws_transport_error == -14);

  connect_client(31);
  cyw43_arch_lwip_begin();
  disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR);
  cyw43_arch_lwip_end();
  // Do not pair a new disconnect with the previous session's close details.
  assert(stats().last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR &&
         stats().last_ws_close_reason == 0 && stats().last_ws_transport_error == 0);

  // The ended session's transport reason can arrive after a replacement
  // connects; record it without disturbing that session.
  connect_client(32);
  close_client(31, Reason::tcp_write_error, -16);
  assert(connected && active_connection == 32);
  assert(stats().last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR &&
         stats().last_ws_close_reason == (uint32_t)Reason::tcp_write_error &&
         stats().last_ws_transport_error == -16);

  cyw43_arch_lwip_begin();
  disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
  cyw43_arch_lwip_end();
  close_client(31, Reason::tcp_write_error, -16);
  assert(stats().last_ws_close_reason == 0);
  close_client(32, Reason::server_abort);
  assert(stats().last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR &&
         stats().last_ws_close_reason == (uint32_t)Reason::server_abort);

  connect_client(33);
  close_client(33);
  close_client(32, Reason::tcp_write_error, -16);
  assert(stats().last_disconnect_reason == Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED &&
         stats().last_ws_close_reason == (uint32_t)Reason::peer_tcp_closed &&
         stats().last_ws_transport_error == 0);

  const char *const disconnects[] = {
      "none", "socket-closed", "network-lost", "output-ack-timeout",
      "pong-timeout", "protocol-error", "output-send-error", "none"};
  for (uint32_t reason = 0; reason < 8; ++reason)
    assert(std::string(z80_web_terminal_disconnect_name(reason)) == disconnects[reason]);
  assert(std::string(z80_web_terminal_ws_close_name((uint32_t)Reason::unknown)) == "unknown");
  assert(std::string(z80_web_terminal_ws_close_name((uint32_t)Reason::pong_backpressure)) ==
         "pong-backpressure");
  assert(std::string(z80_web_terminal_ws_close_name((uint32_t)Reason::local_close)) ==
         "local-close");
  assert(std::string(z80_web_terminal_ws_close_name(99)) == "unknown");
  terminal_diagnostics.clear();
}

int main() {
  struct netif interface = {};
  netif_default = &interface;
  assign_address(interface, "192.0.2.1");
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
  assert(std::string(interface.hostname) == "z80-nexus-123456");
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
  assert(server_started && server->listening_port == 80);
  assert(power_mode_calls == 2 &&
         power_mode_setting == CYW43_NO_POWERSAVE_MODE);
  assert(time_client_starts == 1 && std::string(service_address) == "192.0.2.1");
  // An unchanged binary address is neither re-formatted nor re-published.
  unsigned formats = address_formats, publishes = address_publishes;
  for (unsigned i = 0; i < 3; ++i) {
    advance(1000);
    assert(poll_network());
  }
  assert(address_formats == formats && address_publishes == publishes);
  assign_address(interface, "198.51.100.42");
  unsigned unchanged_polls = polls;
  assert(poll_network() && polls == unchanged_polls + 1);
  assert(std::string(service_address) == "192.0.2.1");
  advance(1000);
  assert(poll_network());
  assert(std::string(service_address) == assigned_address);
  assign_address(interface, "0.0.0.0");
  advance(1000);
  assert(poll_network() && service_address[0] == '\0');
  formats = address_formats;
  advance(1000);  // No address: nothing to format.
  assert(poll_network() && address_formats == formats);
  assign_address(interface, "192.0.2.1");
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
  assert(server->max_message_size == HEADER_BYTES + INPUT_BYTES);
  test_handshake();
  ready_client(1);
  test_last_connection_wins();
  test_input_flow_control();
  test_output_window();
  test_output_coalescing();
  test_complete_redraw();
  test_heartbeat();
  test_handover();
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
  test_protocol_errors();
  connect_client(90);
  // Controls from another socket or before READY cannot request a reboot.
  message(91, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_IDLE && connected);
  message(90, REBOOT, 0);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_IDLE && !connected);
  ready_client(92);
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
  assert(sent[0] == REBOOT_STATUS && sent_sequence() == Z80_WEB_REBOOT_FLUSHING);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_STORAGE_ERROR);
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS && sent_sequence() == Z80_WEB_REBOOT_STORAGE_ERROR);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_RESTARTING);
  unsigned close_count = closed.size();
  poll_terminal();
  assert(sent[0] == REBOOT_STATUS && sent_sequence() == Z80_WEB_REBOOT_RESTARTING);
  assert(closed.size() == close_count + 1 && closed.back() == 92);
  poll_terminal();
  assert(closed.size() == close_count + 1); // One close per status transition.
  close_client(92, Reason::local_close);
  // A live browser/network core cannot depend on core 0 servicing its loop.
  // Keep the original deadline even when the browser reconnects or retries.
  const uint32_t reboot_deadline_ms = REBOOT_DEADLINE_US / 1000;
  reboot_status.store(Z80_WEB_REBOOT_IDLE);
  ready_client(93);
  message(93, REBOOT, 0);
  uint64_t original_deadline = reboot_deadline;
  advance(1000);
  message(93, REBOOT, 0);
  assert(reboot_deadline == original_deadline);
  close_client(93);
  ready_client(94);
  advance(reboot_deadline_ms - 1001);
  poll_terminal();
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_REQUESTED);
  advance(1);
  poll_terminal();
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_TIMEOUT);
  assert(!z80_web_terminal_begin_reboot());
  message(94, REBOOT, 0);
  z80_web_terminal_set_reboot_status(Z80_WEB_REBOOT_RESTARTING);
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_TIMEOUT);
  close_client(94);
  ready_client(95);
  assert(sent[0] == REBOOT_STATUS && sent_sequence() == Z80_WEB_REBOOT_TIMEOUT);
  close_client(95);

  reboot_status.store(Z80_WEB_REBOOT_IDLE);
  ready_client(96);
  message(96, REBOOT, 0);
  assert(z80_web_terminal_begin_reboot());
  assert(!z80_web_terminal_begin_reboot());
  advance(reboot_deadline_ms);
  poll_terminal();
  assert(z80_web_terminal_reboot_status() == Z80_WEB_REBOOT_TIMEOUT);
  close_client(96);
  assert(terminal_diagnostics.find("[network] Terminal: http://") ==
         std::string::npos);
  delete server;
  puts("PASS: protocol 5 handshake, four-frame window, cumulative/wrapping ACKs, "
       "ordered backpressure without retransmission, batched flush, pending input "
       "and ACK recovery, strict sequencing, network lifecycle, heartbeat and "
       "USB preservation");
}
