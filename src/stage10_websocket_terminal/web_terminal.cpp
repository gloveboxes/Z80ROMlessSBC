#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico_ws_server/web_socket_server.h"
#include "web_terminal.h"
#include "z80sbc/terminal.h"

// Core 1 owns this module. The WebSocket callbacks run inside
// cyw43_arch_poll() or z80_web_terminal_poll(), never in an interrupt. Only
// the reboot status and the published statistics are shared with core 0.
//
// Callbacks only record state; service_session() does all the sending, after
// cyw43_arch_poll() has drained the radio. A frame sent from the receive path
// can run the CYW43 driver out of transmit credit while received frames are
// still queued in the chip, and the driver discards the frames it reads while
// it waits for credit. The browser then stalls until TCP retransmits them.
namespace {
constexpr uint16_t SERVER_PORT = Z80_WEB_TERMINAL_PORT;
constexpr uint32_t SERVER_CONNECTIONS = 4;
constexpr uint64_t SERVER_RETRY_US = 5u * 1000000u;
constexpr size_t FRAME_BYTES = Z80_WEB_TERMINAL_FRAME_BYTES;
constexpr size_t INPUT_BYTES = 64;
constexpr size_t HEADER_BYTES = 5;
constexpr uint32_t OUTPUT_WINDOW = Z80_WEB_TERMINAL_OUTPUT_WINDOW;
constexpr uint64_t OUTPUT_COALESCE_US = 1000;
constexpr uint32_t PROTOCOL_VERSION = 5;
constexpr uint64_t REBOOT_DEADLINE_US = 35u * 1000000u;
constexpr uint64_t HEARTBEAT_US = 30u * 1000000u;
constexpr uint64_t PONG_TIMEOUT_US = 20u * 1000000u;
constexpr uint64_t PING_RETRY_US = 250u * 1000u;
// Tolerate two misses; disconnect on the third consecutive miss.
constexpr uint8_t MAX_MISSED_PONGS = 2;
// xterm ACKs output only after its write callback runs; allow a backgrounded
// or busy browser to drain the output window before restoring USB.
constexpr uint64_t OUTPUT_ACK_TIMEOUT_US = 5u * 60u * 1000000ull;
enum : uint8_t {
  OUTPUT = 1, INPUT = 2, OUTPUT_ACK = 3, INPUT_ACK = 4,
  HELLO = 5, READY = 6, REBOOT = 7, REBOOT_STATUS = 8,
};

WebSocketServer *server;
bool server_started;
uint64_t server_retry_deadline;
int32_t active_connection = -1;
// A session this module ended; its transport close reason is still to come.
int32_t closing_connection = -1;
uint32_t session_network_generation;
bool network_online;

bool protocol_ready;
bool hello_pending;
// TCP owns each frame once it accepts it, so only the next frame is held.
uint8_t staged[HEADER_BYTES + FRAME_BYTES];
size_t staged_length;
// Sequences start at 1; output_acked is the last one the browser consumed.
uint32_t output_sequence = 1;
uint32_t output_acked;
uint64_t output_batch_deadline;
uint64_t output_ack_deadline;
uint8_t pending_input[INPUT_BYTES];
size_t pending_input_length;
uint32_t input_sequence = 1;
bool input_ack_pending;

uint64_t ping_deadline;
uint64_t pong_deadline;
uint32_t ping_sequence;
uint8_t ping_payload[4];
bool awaiting_pong;
bool activity_since_ping;
uint8_t missed_pongs;

std::atomic<uint32_t> reboot_status{Z80_WEB_REBOOT_IDLE};
uint32_t reported_reboot_status = UINT32_MAX;
// Core 1 owns this deadline. Keep it across browser disconnects so core 0
// cannot leave a request looking active forever if its main loop stalls.
uint64_t reboot_deadline;
bool reboot_deadline_active;

z80_web_terminal_stats_t terminal_stats;

template <typename T> void publish(T &field, T value) {
  __atomic_store_n(&field, value, __ATOMIC_RELAXED);
}

template <typename T> T published(const T &field) {
  return __atomic_load_n(&field, __ATOMIC_RELAXED);
}

void count(uint32_t &counter) { publish(counter, published(counter) + 1); }

uint32_t frames_in_flight() { return output_sequence - output_acked - 1; }

void publish_output_usage() {
  uint32_t frames = frames_in_flight();
  publish(terminal_stats.window_frames, frames);
  publish(terminal_stats.staged_bytes, (uint32_t)staged_length);
  if (frames > published(terminal_stats.window_frames_peak))
    publish(terminal_stats.window_frames_peak, frames);
}

void publish_close(uint32_t disconnect_reason, WebSocketServer::CloseReason reason,
                   int transport_error) {
  if (disconnect_reason != Z80_WEB_TERMINAL_DISCONNECT_NONE)
    publish(terminal_stats.last_disconnect_reason, disconnect_reason);
  publish(terminal_stats.last_ws_close_reason, (uint32_t)reason);
  publish(terminal_stats.last_ws_transport_error, (int32_t)transport_error);
}

uint32_t read_sequence(const uint8_t *data) {
  return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
         (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void write_sequence(uint8_t *data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data[i] = (uint8_t)(value >> (8 * i));
}

void reset_session() {
  protocol_ready = hello_pending = input_ack_pending = false;
  reported_reboot_status = UINT32_MAX;
  input_sequence = output_sequence = 1;
  output_acked = 0;
  staged_length = pending_input_length = 0;
  output_batch_deadline = 0;
  awaiting_pong = activity_since_ping = false;
  missed_pongs = 0;
  publish_output_usage();
}

void disconnect_client(z80_web_terminal_disconnect_reason_t reason) {
  if (active_connection < 0)
    return;
  closing_connection = active_connection;
  active_connection = -1;
  // Do not pair this disconnect with an earlier session's close details.
  publish_close(reason, WebSocketServer::CloseReason::unknown, 0);
  if (!server->abort((uint32_t)closing_connection))
    printf("[network] Terminal connection already removed during teardown.\n");
  z80_terminal_browser_disconnect();
  reset_session();
}

void protocol_error(const char *problem) {
  printf("[network] FAIL: %s; reload the terminal page.\n", problem);
  disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
}

bool send_control(uint8_t type, uint32_t sequence) {
  uint8_t frame[HEADER_BYTES] = {type};
  write_sequence(frame + 1, sequence);
  return server->sendMessage((uint32_t)active_connection, frame, sizeof(frame));
}

void check_reboot_deadline() {
  if (!reboot_deadline_active)
    return;
  uint32_t status = reboot_status.load(std::memory_order_acquire);
  while (status == Z80_WEB_REBOOT_REQUESTED || status == Z80_WEB_REBOOT_FLUSHING ||
         status == Z80_WEB_REBOOT_RESTARTING) {
    if (time_us_64() < reboot_deadline)
      return;
    if (reboot_status.compare_exchange_weak(status, Z80_WEB_REBOOT_TIMEOUT,
                                             std::memory_order_acq_rel)) {
      printf("[web] Reboot timed out waiting for supervisor progress. "
             "No forced reset; inspect USB supervisor status.\n");
      break;
    }
  }
  reboot_deadline_active = false;
}

void request_reboot() {
  uint32_t status = reboot_status.load(std::memory_order_acquire);
  // Repeated commands cannot restart an active operation. Failure can be
  // retried, but only core 0 decides whether it is safe to proceed.
  while (status != Z80_WEB_REBOOT_REQUESTED &&
         status != Z80_WEB_REBOOT_FLUSHING &&
         status != Z80_WEB_REBOOT_RESTARTING &&
         status != Z80_WEB_REBOOT_TIMEOUT) {
    if (reboot_status.compare_exchange_weak(status, Z80_WEB_REBOOT_REQUESTED,
                                             std::memory_order_acq_rel)) {
      reboot_deadline = time_us_64() + REBOOT_DEADLINE_US;
      reboot_deadline_active = true;
      break;
    }
  }
  reported_reboot_status = UINT32_MAX;
}

void report_reboot_status() {
  uint32_t status = reboot_status.load(std::memory_order_acquire);
  if (protocol_ready && status != reported_reboot_status &&
      send_control(REBOOT_STATUS, status)) {
    reported_reboot_status = status;
    // A watchdog reset sends no TCP FIN. Close explicitly while the network is
    // still alive so the browser can reconnect without waiting for TCP timeout.
    if (status == Z80_WEB_REBOOT_RESTARTING &&
        !server->close((uint32_t)active_connection))
      printf("[network] Close frame not sent; connection scheduled for cleanup.\n");
  }
}

// ACK means queued for the Z80. A full RX queue holds the one frame the
// browser may send until there is room; nothing is evicted to make space.
void receive_input(const uint8_t *data, size_t length) {
  if (!z80_terminal_browser_try_receive(data, length)) {
    if (data != pending_input)
      memcpy(pending_input, data, length);
    pending_input_length = length;
    return;
  }
  pending_input_length = 0;
  ++input_sequence;
  input_ack_pending = true;
}

void pump_output(uint64_t now) {
  bool sent = false;
  bool changed = false;
  for (;;) {
    size_t supplied = z80_terminal_supply(staged + HEADER_BYTES + staged_length,
                                          FRAME_BYTES - staged_length);
    staged_length += supplied;
    changed |= supplied != 0;
    if (!staged_length || frames_in_flight() == OUTPUT_WINDOW)
      break;
    // Send a full frame now; let a partial one gather bytes for a moment.
    if (staged_length < FRAME_BYTES && now < output_batch_deadline)
      break;
    staged[0] = OUTPUT;
    write_sequence(staged + 1, output_sequence);
    auto result = server->enqueueMessage((uint32_t)active_connection, staged,
                                         HEADER_BYTES + staged_length);
    if (result == WebSocketServer::SendResult::would_block)
      break;
    if (result != WebSocketServer::SendResult::queued) {
      printf("[network] FAIL: terminal output enqueue (%u); restoring USB.\n",
             (unsigned)result);
      disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR);
      return;
    }
    output_ack_deadline = now + OUTPUT_ACK_TIMEOUT_US;
    ++output_sequence;
    staged_length = 0;
    output_batch_deadline = now + OUTPUT_COALESCE_US;
    sent = changed = true;
  }
  if (changed)
    publish_output_usage();
  if (sent && !server->flush((uint32_t)active_connection))
    count(terminal_stats.flush_failures);
}

void acknowledge_output(uint32_t sequence) {
  uint32_t consumed = sequence - output_acked;
  if (!consumed || consumed > frames_in_flight()) {
    protocol_error("unexpected terminal output acknowledgement");
    return;
  }
  output_acked = sequence;
  output_ack_deadline = time_us_64() + OUTPUT_ACK_TIMEOUT_US;
  publish_output_usage();
}

void note_browser_activity() {
  missed_pongs = 0;
  if (awaiting_pong)
    activity_since_ping = true;
}

void on_connect(WebSocketServer &websocket, uint32_t connection) {
  if (active_connection >= 0) {
    // Last connection wins. The abort reclaims the old slot without waiting
    // for a close reply.
    closing_connection = active_connection;
    if (!websocket.abort((uint32_t)closing_connection))
      printf("[network] Previous terminal connection already removed.\n");
    printf("[network] New browser replaced the previous terminal connection.\n");
  }
  active_connection = (int32_t)connection;
  reset_session();
  z80_terminal_browser_connect();
  hello_pending = true;
}

void on_close(WebSocketServer &, uint32_t connection,
              WebSocketServer::CloseReason reason, int transport_error) {
  if ((int32_t)connection == closing_connection) {
    closing_connection = -1;
    publish_close(Z80_WEB_TERMINAL_DISCONNECT_NONE, reason, transport_error);
    return;
  }
  if ((int32_t)connection != active_connection)
    return;
  active_connection = -1;
  publish_close(Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED, reason, transport_error);
  printf("[network] Terminal connection closed: %s",
         z80_web_terminal_ws_close_name((uint32_t)reason));
  if (transport_error)
    printf(" (lwIP error=%d)", transport_error);
  printf("; restoring USB.\n");
  z80_terminal_browser_disconnect();
  reset_session();
}

void on_message(WebSocketServer &, uint32_t connection,
                const void *data, size_t length) {
  if ((int32_t)connection != active_connection)
    return;
  const uint8_t *frame = static_cast<const uint8_t *>(data);
  if (length < HEADER_BYTES) {
    protocol_error("invalid terminal protocol");
    return;
  }
  note_browser_activity();
  const uint8_t type = frame[0];
  const uint32_t sequence = read_sequence(frame + 1);
  const size_t payload = length - HEADER_BYTES;
  if (type == READY && !payload && sequence == PROTOCOL_VERSION) {
    if (!protocol_ready) {
      protocol_ready = true;
      ping_deadline = time_us_64() + HEARTBEAT_US;
    }
  } else if (!protocol_ready) {
    protocol_error("terminal frame before the handshake");
  } else if (type == REBOOT && !payload && sequence == 0) {
    request_reboot();
  } else if (type == OUTPUT_ACK && !payload) {
    acknowledge_output(sequence);
  } else if (type == INPUT && payload && payload <= INPUT_BYTES) {
    // The browser sends one frame at a time and waits for its ACK.
    if (sequence != input_sequence || pending_input_length || input_ack_pending)
      protocol_error("out-of-order terminal input");
    else
      receive_input(frame + HEADER_BYTES, payload);
  } else {
    protocol_error("invalid terminal frame");
  }
}

void on_pong(WebSocketServer &, uint32_t connection,
             const void *data, size_t length) {
  if ((int32_t)connection != active_connection)
    return;
  if (awaiting_pong && length == sizeof(ping_payload) &&
      memcmp(data, ping_payload, length) == 0) {
    awaiting_pong = false;
    missed_pongs = 0;
    activity_since_ping = false;
  } else {
    // Even a late PONG proves the peer is responsive and can satisfy a
    // newer ping's liveness check through note_browser_activity().
    note_browser_activity();
  }
}

bool ensure_server() {
  if (server_started)
    return true;
  uint64_t now = time_us_64();
  if (now < server_retry_deadline)
    return false;
  if (server == nullptr) {
    server = new (std::nothrow) WebSocketServer(SERVER_CONNECTIONS);
    if (server == nullptr) {
      printf("[network] FAIL: server allocation; retry in 5 seconds.\n");
      server_retry_deadline = now + SERVER_RETRY_US;
      return false;
    }
    server->setConnectCallback(on_connect);
    server->setCloseCallback(on_close);
    server->setMessageCallback(on_message);
    server->setPongCallback(on_pong);
    server->setTcpNoDelay(true);
    if (!server->setMaxMessageSize(HEADER_BYTES + INPUT_BYTES)) {
      printf("[network] FAIL: terminal message limit; retry in 5 seconds.\n");
      delete server;
      server = nullptr;
      server_retry_deadline = now + SERVER_RETRY_US;
      return false;
    }
  }
  server_started = server->startListening(SERVER_PORT);
  if (!server_started) {
    printf("[network] FAIL: HTTP/WebSocket listen; retry in 5 seconds.\n");
    server_retry_deadline = now + SERVER_RETRY_US;
  }
  return server_started;
}

// False when the browser missed too many heartbeats and was disconnected.
bool service_heartbeat(uint64_t now) {
  if (awaiting_pong) {
    if (now < pong_deadline)
      return true;
    awaiting_pong = false;
    if (activity_since_ping) {
      missed_pongs = 0;
    } else {
      count(terminal_stats.pong_timeouts);
      if (++missed_pongs > MAX_MISSED_PONGS) {
        printf("[network] Terminal client missed too many PONGs; restoring USB.\n");
        disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PONG_TIMEOUT);
        return false;
      }
    }
    activity_since_ping = false;
  }
  if (now >= ping_deadline) {
    write_sequence(ping_payload, ++ping_sequence);
    if (server->sendPing((uint32_t)active_connection, ping_payload,
                         sizeof(ping_payload))) {
      awaiting_pong = true;
      activity_since_ping = false;
      ping_deadline = now + HEARTBEAT_US;
      pong_deadline = now + PONG_TIMEOUT_US;
    } else {
      ping_deadline = now + PING_RETRY_US;
    }
  }
  return true;
}

void service_session() {
  const uint64_t now = time_us_64();
  // A control stays pending until TCP takes it.
  if (hello_pending)
    hello_pending = !send_control(HELLO, PROTOCOL_VERSION);
  if (pending_input_length)
    receive_input(pending_input, pending_input_length);
  if (input_ack_pending)
    input_ack_pending = !send_control(INPUT_ACK, input_sequence - 1);
  report_reboot_status();
  if (frames_in_flight() && now >= output_ack_deadline) {
    count(terminal_stats.ack_timeouts);
    printf("[network] Terminal client timed out; restoring USB.\n");
    disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_ACK_TIMEOUT);
    return;
  }
  if (protocol_ready && service_heartbeat(now))
    pump_output(now);
}
}  // namespace

extern "C" z80_web_terminal_stats_t z80_web_terminal_get_stats(void) {
  z80_web_terminal_stats_t stats;
  stats.flush_failures = published(terminal_stats.flush_failures);
  stats.ack_timeouts = published(terminal_stats.ack_timeouts);
  stats.pong_timeouts = published(terminal_stats.pong_timeouts);
  stats.window_frames = published(terminal_stats.window_frames);
  stats.window_frames_peak = published(terminal_stats.window_frames_peak);
  stats.staged_bytes = published(terminal_stats.staged_bytes);
  stats.last_disconnect_reason = published(terminal_stats.last_disconnect_reason);
  stats.last_ws_close_reason = published(terminal_stats.last_ws_close_reason);
  stats.last_ws_transport_error = published(terminal_stats.last_ws_transport_error);
  return stats;
}

extern "C" const char *z80_web_terminal_disconnect_name(uint32_t reason) {
  static const char *const names[] = {
      "none", "socket-closed", "network-lost", "output-ack-timeout",
      "pong-timeout", "protocol-error", "output-send-error",
  };
  return reason < sizeof(names) / sizeof(names[0]) ? names[reason] : "none";
}

extern "C" const char *z80_web_terminal_ws_close_name(uint32_t reason) {
  using Reason = WebSocketServer::CloseReason;
  static const char *const names[] = {
      "unknown", "peer-tcp-closed", "peer-ws-close", "invalid-ws-input",
      "pong-backpressure", "tcp-write-error", "tcp-output-error", "tcp-error",
      "close-timeout", "server-abort", "local-close",
  };
  static_assert((unsigned)Reason::unknown == 0 &&
                (unsigned)Reason::peer_tcp_closed == 1 &&
                (unsigned)Reason::peer_websocket_close == 2 &&
                (unsigned)Reason::invalid_websocket_input == 3 &&
                (unsigned)Reason::pong_backpressure == 4 &&
                (unsigned)Reason::tcp_write_error == 5 &&
                (unsigned)Reason::tcp_output_error == 6 &&
                (unsigned)Reason::tcp_error == 7 &&
                (unsigned)Reason::close_timeout == 8 &&
                (unsigned)Reason::server_abort == 9 &&
                (unsigned)Reason::local_close == 10 &&
                sizeof(names) / sizeof(names[0]) == 11);
  return reason < sizeof(names) / sizeof(names[0]) ? names[reason] : "unknown";
}

extern "C" void z80_web_terminal_poll(bool online, uint32_t generation) {
  check_reboot_deadline();
  if (!online && !network_online && generation == session_network_generation)
    return;
  if (!online && server == nullptr) {
    session_network_generation = generation;
    return;
  }
  cyw43_arch_lwip_begin();
  if (!online || generation != session_network_generation) {
    disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_NETWORK_LOST);
    session_network_generation = generation;
  }
  network_online = online;
  if (online)
    ensure_server();
  // Also runs offline, to reclaim the connection just disconnected.
  if (server != nullptr)
    server->poll();
  if (active_connection >= 0)
    service_session();
  cyw43_arch_lwip_end();
}

extern "C" z80_web_reboot_status_t z80_web_terminal_reboot_status(void) {
  return (z80_web_reboot_status_t)reboot_status.load(std::memory_order_acquire);
}

extern "C" bool z80_web_terminal_begin_reboot(void) {
  uint32_t expected = Z80_WEB_REBOOT_REQUESTED;
  return reboot_status.compare_exchange_strong(expected, Z80_WEB_REBOOT_FLUSHING,
                                                std::memory_order_acq_rel);
}

extern "C" void z80_web_terminal_set_reboot_status(z80_web_reboot_status_t status) {
  if (status == Z80_WEB_REBOOT_CPM_READY) {
    uint32_t expected = Z80_WEB_REBOOT_IDLE;
    reboot_status.compare_exchange_strong(expected, status, std::memory_order_acq_rel);
  } else {
    uint32_t previous = reboot_status.load(std::memory_order_acquire);
    // Timeout is terminal for this boot. A late supervisor must not overwrite
    // it with progress, or allow a browser retry to resurrect a cancelled stop.
    while (previous != Z80_WEB_REBOOT_TIMEOUT &&
           !reboot_status.compare_exchange_weak(previous, status,
                                                std::memory_order_acq_rel)) {}
  }
}
