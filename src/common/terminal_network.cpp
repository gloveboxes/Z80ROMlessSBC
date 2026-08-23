#include <atomic>
#include <cstdint>
#include <new>

#include "cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico_ws_server/web_socket_server.h"
#include "wifi_config.h"
#include "z80sbc/terminal.h"

namespace {

constexpr uint16_t SERVER_PORT = 8088;
constexpr uint32_t SERVER_CONNECTIONS = 4;
constexpr uint32_t RETRY_DELAY_MS = 5000;
constexpr uint32_t ASSOCIATION_TIMEOUT_MS = 30000;
constexpr size_t FRAME_BYTES = 256;

enum class WifiState {
  uninitialized,
  init_backoff,
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
std::atomic<int32_t> active_connection{-1};
std::atomic<int32_t> pending_reject{-1};
std::atomic<bool> pending_connected{false};
std::atomic<bool> pending_disconnected{false};

int link_status() {
  cyw43_arch_lwip_begin();
  int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  return status;
}

void on_connect(WebSocketServer &websocket, uint32_t connection) {
  (void)websocket;
  int32_t expected = -1;
  if (active_connection.compare_exchange_strong(
          expected, (int32_t)connection, std::memory_order_acq_rel)) {
    pending_connected.store(true, std::memory_order_release);
  } else {
    pending_reject.store((int32_t)connection, std::memory_order_release);
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
  z80_terminal_network_receive(static_cast<const uint8_t *>(data), length);
}

void process_events() {
  int32_t reject = pending_reject.exchange(-1, std::memory_order_acq_rel);
  if (reject >= 0 && server != nullptr)
    server->close((uint32_t)reject);
  if (pending_connected.exchange(false, std::memory_order_acq_rel))
    z80_terminal_network_connected();
  if (pending_disconnected.exchange(false, std::memory_order_acq_rel))
    z80_terminal_network_disconnected();
}

void disconnect_client() {
  int32_t connection =
      active_connection.exchange(-1, std::memory_order_acq_rel);
  if (connection >= 0 && server != nullptr)
    server->close((uint32_t)connection);
  pending_connected.store(false, std::memory_order_release);
  pending_disconnected.store(false, std::memory_order_release);
  z80_terminal_network_disconnected();
}

bool start_association() {
  int result = cyw43_arch_wifi_connect_async(
      Z80_WIFI_SSID, Z80_WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
  if (result == PICO_OK) {
    association_deadline = make_timeout_time_ms(ASSOCIATION_TIMEOUT_MS);
    wifi_state = WifiState::associating;
    return true;
  }
  retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
  wifi_state = WifiState::association_backoff;
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
    if (server == nullptr)
      return false;
    server->setConnectCallback(on_connect);
    server->setCloseCallback(on_close);
    server->setMessageCallback(on_message);
    server->setTcpNoDelay(true);
  }
  server_started = server->startListening(SERVER_PORT);
  if (!server_started) {
    server_retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    server_retry_pending = true;
  }
  return server_started;
}

}  // namespace

extern "C" bool z80_terminal_network_poll(void) {
  if (Z80_WIFI_SSID[0] == '\0')
    return false;

  if (wifi_state == WifiState::uninitialized) {
    if (cyw43_arch_init() != PICO_OK) {
      cyw43_arch_deinit();
      retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
      wifi_state = WifiState::init_backoff;
      return false;
    }
    cyw43_arch_enable_sta_mode();
    start_association();
    return false;
  }

  if (wifi_state == WifiState::init_backoff) {
    if (time_reached(retry_deadline))
      wifi_state = WifiState::uninitialized;
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
      wifi_state = WifiState::online;
    } else if (status < CYW43_LINK_DOWN ||
               time_reached(association_deadline)) {
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
      wifi_state = WifiState::association_backoff;
      return false;
    } else {
      return false;
    }
  }

  if (wifi_state == WifiState::online && status != CYW43_LINK_UP) {
    disconnect_client();
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    wifi_state = WifiState::association_backoff;
    return false;
  }

  return ensure_server();
}

extern "C" void z80_terminal_network_poll_input(void) {
  if (server == nullptr || !server_started)
    return;
  server->popMessages();
  process_events();
}

extern "C" void z80_terminal_network_poll_output(void) {
  if (server == nullptr || !server_started)
    return;
  int32_t connection = active_connection.load(std::memory_order_acquire);
  if (connection < 0)
    return;

  uint8_t payload[FRAME_BYTES];
  size_t length = z80_terminal_network_supply(payload, sizeof(payload));
  if (length != 0 &&
      !server->sendMessage((uint32_t)connection, payload, length))
    z80_terminal_network_tx_dropped(length);
}