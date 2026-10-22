#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico_ws_server/web_socket_server.h"
#include "network_setup.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "z80sbc/io_ports.h"
#include "z80sbc/terminal.h"
#include "io_service.h"

namespace {

constexpr uint16_t SERVER_PORT = 8088;
constexpr uint32_t SERVER_CONNECTIONS = 4;
constexpr uint32_t RETRY_DELAY_MS = 5000;
constexpr uint32_t ASSOCIATION_TIMEOUT_MS = 30000;
constexpr size_t FRAME_BYTES = 256;
constexpr size_t INPUT_BYTES = 64;
constexpr size_t HEADER_BYTES = 5;
constexpr uint32_t FRAME_RETRY_MS = 250;
constexpr uint32_t HEARTBEAT_MS = 5000;
constexpr uint32_t CLIENT_TIMEOUT_MS = 15000;
enum : uint8_t {
  OUTPUT = 1, INPUT = 2, OUTPUT_ACK = 3, INPUT_ACK = 4,
  HELLO = 5, READY = 6,
};

enum class WifiState {
  uninitialized,
  associating,
  online,
  association_backoff,
};

WifiState wifi_state = WifiState::uninitialized;
absolute_time_t retry_deadline = nil_time;
absolute_time_t association_deadline = nil_time;
absolute_time_t server_retry_deadline = nil_time;
WebSocketServer *server = nullptr;
bool server_started = false;
bool server_retry_pending = false;
bool time_client_started = false;
z80_network_config_t active_config = {};
std::atomic<int32_t> active_connection{-1};
std::atomic<bool> pending_connected{false};
std::atomic<bool> pending_disconnected{false};
bool protocol_ready;
uint32_t output_sequence;
uint32_t input_sequence;
uint8_t output_frame[HEADER_BYTES + FRAME_BYTES];
size_t output_length;
absolute_time_t output_retry;
absolute_time_t output_deadline;
absolute_time_t hello_retry;
absolute_time_t client_deadline;
absolute_time_t ping_retry;
uint32_t ping_sequence;
uint8_t ping_payload[4];
bool awaiting_pong;

uint32_t read_sequence(const uint8_t *data) {
  return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
         (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void write_sequence(uint8_t *data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data[i] = (uint8_t)(value >> (8 * i));
}

void disconnect_client();

void reset_session() {
  protocol_ready = false;
  input_sequence = output_sequence = 1;
  output_length = 0;
  output_retry = hello_retry = ping_retry = nil_time;
  client_deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
  awaiting_pong = false;
}

bool send_control(uint8_t type, uint32_t sequence) {
  uint8_t frame[HEADER_BYTES] = {type};
  write_sequence(frame + 1, sequence);
  return server->sendMessage((uint32_t)active_connection.load(), frame,
                             sizeof(frame));
}

int link_status() {
  cyw43_arch_lwip_begin();
  int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  return status;
}

void on_connect(WebSocketServer &websocket, uint32_t connection) {
  int32_t expected = -1;
  if (active_connection.compare_exchange_strong(
          expected, (int32_t)connection, std::memory_order_acq_rel)) {
    pending_connected.store(true, std::memory_order_release);
  } else {
    websocket.close(connection);
    printf("[network] Terminal busy; rejected another browser.\n");
  }
}

void on_close(WebSocketServer &websocket, uint32_t connection) {
  (void)websocket;
  int32_t expected = (int32_t)connection;
  if (active_connection.compare_exchange_strong(
          expected, -1, std::memory_order_acq_rel))
    pending_disconnected.store(true, std::memory_order_release);
}

void on_message(WebSocketServer &websocket, uint32_t connection,
                const void *data, size_t length) {
  (void)websocket;
  if (active_connection.load(std::memory_order_acquire) !=
      (int32_t)connection)
    return;
  const uint8_t *frame = static_cast<const uint8_t *>(data);
  if (length < HEADER_BYTES) {
    printf("[network] FAIL: invalid terminal protocol; reload the terminal page.\n");
    disconnect_client();
    return;
  }
  uint32_t sequence = read_sequence(frame + 1);
  if (frame[0] == READY && length == HEADER_BYTES && sequence == 1) {
    protocol_ready = true;
  } else if (frame[0] == OUTPUT_ACK && length == HEADER_BYTES) {
    if (output_length && sequence == output_sequence) {
      output_length = 0;
      ++output_sequence;
      output_retry = nil_time;
    }
  } else if (frame[0] == INPUT && protocol_ready &&
             length > HEADER_BYTES && length <= HEADER_BYTES + INPUT_BYTES) {
    if (sequence == input_sequence) {
      if (!z80_terminal_network_try_receive(frame + HEADER_BYTES,
                                            length - HEADER_BYTES))
        return;
      ++input_sequence;
    } else if (sequence != input_sequence - 1) {
      printf("[network] FAIL: out-of-order terminal input; disconnecting.\n");
      disconnect_client();
      return;
    }
    // A lost ACK is harmless: retransmitted input is acknowledged, not repeated.
    send_control(INPUT_ACK, sequence);
  } else {
    printf("[network] FAIL: invalid terminal frame; reload the terminal page.\n");
    disconnect_client();
  }
}

void on_pong(WebSocketServer &websocket, uint32_t connection,
             const void *data, size_t length) {
  (void)websocket;
  if (active_connection.load() == (int32_t)connection && awaiting_pong &&
      length == sizeof(ping_payload) &&
      memcmp(data, ping_payload, length) == 0) {
    awaiting_pong = false;
    client_deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
    ping_retry = make_timeout_time_ms(HEARTBEAT_MS);
  }
}

void process_events() {
  if (pending_disconnected.exchange(false, std::memory_order_acq_rel))
    z80_terminal_network_disconnected();
  if (pending_connected.exchange(false, std::memory_order_acq_rel) &&
      active_connection.load(std::memory_order_acquire) >= 0)
  {
    reset_session();
    z80_terminal_network_connected();
  }
}

void disconnect_client() {
  cyw43_arch_lwip_begin();
  int32_t connection =
      active_connection.exchange(-1, std::memory_order_acq_rel);
  if (connection >= 0 && server != nullptr)
    server->close((uint32_t)connection);
  pending_connected.store(false, std::memory_order_release);
  pending_disconnected.store(false, std::memory_order_release);
  z80_terminal_network_disconnected();
  output_length = 0;
  protocol_ready = false;
  cyw43_arch_lwip_end();
}

bool start_association() {
  int result = cyw43_arch_wifi_connect_async(
      active_config.ssid, active_config.password,
      active_config.password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
  if (result == PICO_OK) {
    association_deadline = make_timeout_time_ms(ASSOCIATION_TIMEOUT_MS);
    wifi_state = WifiState::associating;
    return true;
  }
  retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
  wifi_state = WifiState::association_backoff;
  printf("[network] FAIL: association start=%d; retry in 5 seconds.\n", result);
  return false;
}

bool ensure_server() {
  if (server_started)
    return true;
  if (server_retry_pending && !time_reached(server_retry_deadline))
    return false;
  server_retry_pending = false;
  if (server == nullptr) {
    server = new (std::nothrow) WebSocketServer(SERVER_CONNECTIONS);
    if (server == nullptr) {
      printf("[network] FAIL: server allocation; retry in 5 seconds.\n");
      server_retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
      server_retry_pending = true;
      return false;
    }
    server->setConnectCallback(on_connect);
    server->setCloseCallback(on_close);
    server->setMessageCallback(on_message);
    server->setPongCallback(on_pong);
    server->setTcpNoDelay(true);
  }
  server_started = server->startListening(SERVER_PORT);
  if (!server_started) {
    printf("[network] FAIL: HTTP/WebSocket listen; retry in 5 seconds.\n");
    server_retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    server_retry_pending = true;
  }
  return server_started;
}

void refresh_network_address() {
  char address[16] = {};
  cyw43_arch_lwip_begin();
  if (netif_default != nullptr &&
      ip4addr_ntoa_r(netif_ip4_addr(netif_default), address,
                     sizeof(address)) == nullptr) {
    address[0] = '\0';
    printf("[network] FAIL: IPv4 address formatting.\n");
  }
  cyw43_arch_lwip_end();
  if (strcmp(address, "0.0.0.0") == 0)
    address[0] = '\0';
  char previous[16];
  z80_io_ports_get_network_address(previous, sizeof(previous));
  if (strcmp(previous, address) != 0) {
    z80_io_ports_set_network_address(address);
    if (address[0])
      printf("[network] Terminal: http://%s:%u/\n", address, SERVER_PORT);
  }
}

}  // namespace

extern "C" bool z80_terminal_network_poll(void) {
  if (!z80_wireless_init())
    return false;
  /* The poll architecture runs all lwIP/HTTP/WebSocket callbacks on core 1. */
  cyw43_arch_poll();
  const z80_network_config_t *config = z80_network_config();
  if (config == nullptr) {
    if (wifi_state != WifiState::uninitialized) {
      disconnect_client();
      z80_io_ports_set_network_address("");
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      wifi_state = WifiState::uninitialized;
      active_config = {};
    }
    return false;
  }
  if (memcmp(&active_config, config, sizeof(active_config)) != 0) {
    disconnect_client();
    z80_io_ports_set_network_address("");
    if (wifi_state != WifiState::uninitialized)
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    active_config = *config;
    wifi_state = WifiState::uninitialized;
  }

  if (wifi_state == WifiState::uninitialized) {
    cyw43_arch_enable_sta_mode();
    start_association();
    return false;
  }

  if (wifi_state == WifiState::association_backoff) {
    if (time_reached(retry_deadline))
      start_association();
    return false;
  }

  int status = link_status();
  if (wifi_state == WifiState::associating) {
    if (status == CYW43_LINK_UP) {
      cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
      cyw43_arch_lwip_begin();
      if (!time_client_started) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        time_client_started = true;
      }
      cyw43_arch_lwip_end();
      wifi_state = WifiState::online;
    } else if (status < CYW43_LINK_DOWN ||
               time_reached(association_deadline)) {
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
      wifi_state = WifiState::association_backoff;
      printf("[network] FAIL: association status=%d or timeout; retry in 5 seconds.\n",
             status);
      return false;
    } else {
      return false;
    }
  }

  if (wifi_state == WifiState::online && status != CYW43_LINK_UP) {
    z80_io_ports_set_network_address("");
    disconnect_client();
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    wifi_state = WifiState::association_backoff;
    printf("[network] Wi-Fi link lost; USB terminal restored; retry in 5 seconds.\n");
    return false;
  }

  refresh_network_address();
  return ensure_server();
}

extern "C" void z80_terminal_network_poll_input(void) {
  if (server == nullptr || !server_started)
    return;
  cyw43_arch_lwip_begin();
  process_events();
  server->popMessages();
  cyw43_arch_lwip_end();
}

extern "C" void z80_terminal_network_poll_output(void) {
  if (server == nullptr || !server_started)
    return;
  cyw43_arch_lwip_begin();
  process_events();
  int32_t connection = active_connection.load(std::memory_order_acquire);
  if (connection < 0) {
    cyw43_arch_lwip_end();
    return;
  }

  if (time_reached(client_deadline) ||
      (output_length && time_reached(output_deadline))) {
    printf("[network] Terminal client timed out; restoring USB.\n");
    disconnect_client();
    cyw43_arch_lwip_end();
    return;
  }
  if (time_reached(ping_retry)) {
    if (!awaiting_pong) {
      write_sequence(ping_payload, ++ping_sequence);
      awaiting_pong = true;
    }
    // Retry the same challenge even if the library reports a flush failure.
    server->sendPing((uint32_t)connection, ping_payload, sizeof(ping_payload));
    ping_retry = make_timeout_time_ms(FRAME_RETRY_MS);
  }
  if (!protocol_ready) {
    if (time_reached(hello_retry)) {
      send_control(HELLO, 1);
      hello_retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
    cyw43_arch_lwip_end();
    return;
  }
  if (!output_length) {
    size_t length = z80_terminal_network_supply(output_frame + HEADER_BYTES,
                                                FRAME_BYTES);
    if (length) {
      output_frame[0] = OUTPUT;
      write_sequence(output_frame + 1, output_sequence);
      output_length = HEADER_BYTES + length;
      output_deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
    }
  }
  if (output_length && time_reached(output_retry)) {
    // Keep bytes until a browser ACK, not the ambiguous send/flush return value.
    server->sendMessage((uint32_t)connection, output_frame, output_length);
    output_retry = make_timeout_time_ms(FRAME_RETRY_MS);
  }
  cyw43_arch_lwip_end();
}