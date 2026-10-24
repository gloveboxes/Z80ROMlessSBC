#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico_ws_server/web_socket_server.h"
#include "web_terminal.h"
#include "z80sbc/io_ports.h"
#include "z80sbc/terminal.h"

namespace {
constexpr uint16_t SERVER_PORT = Z80_WEB_TERMINAL_PORT;
constexpr uint32_t SERVER_CONNECTIONS = 4;
constexpr uint32_t SERVER_RETRY_MS = 5000;
constexpr size_t FRAME_BYTES = Z80_WEB_TERMINAL_FRAME_BYTES;
constexpr size_t INPUT_BYTES = 64;
constexpr size_t HEADER_BYTES = 5;
constexpr size_t OUTPUT_WINDOW = Z80_WEB_TERMINAL_OUTPUT_WINDOW;
constexpr uint32_t OUTPUT_COALESCE_US = 1000;
constexpr uint32_t PROTOCOL_VERSION = 3;
constexpr uint32_t FRAME_RETRY_MS = 250;
constexpr uint32_t HEARTBEAT_MS = 30u * 1000u;
constexpr uint32_t PONG_TIMEOUT_MS = 20u * 1000u;
// Tolerate two misses; disconnect on the third consecutive miss.
constexpr uint8_t MAX_MISSED_PONGS = 2;
// xterm ACKs output only after its write callback runs; allow a backgrounded
// or busy browser to drain the bounded output window before restoring USB.
constexpr uint32_t OUTPUT_ACK_TIMEOUT_MS = 5u * 60u * 1000u;
enum : uint8_t {
  OUTPUT = 1, INPUT = 2, OUTPUT_ACK = 3, INPUT_ACK = 4,
  HELLO = 5, READY = 6,
};

struct OutputFrame {
  uint8_t data[HEADER_BYTES + FRAME_BYTES];
  size_t length;
  absolute_time_t retry;
  absolute_time_t deadline;
  bool sent;
};
OutputFrame output_frames[OUTPUT_WINDOW];
size_t output_head;
size_t output_count;
size_t retained_output_bytes;
size_t staged_output_length;
absolute_time_t output_batch_deadline = nil_time;
z80_web_terminal_stats_t terminal_stats;
uint8_t pending_input[INPUT_BYTES];
size_t pending_input_length;
uint32_t output_sequence;
uint32_t input_sequence;
bool input_ack_pending;
bool protocol_ready;
WebSocketServer *server;
bool server_started;
absolute_time_t server_retry_deadline = nil_time;
std::atomic<int32_t> active_connection{-1};
// Retain the closing session's identity until its deferred diagnostics arrive.
std::atomic<int32_t> last_disconnected_connection{-1};
std::atomic<bool> pending_connected{false};
std::atomic<bool> pending_disconnected{false};
std::atomic<uint32_t> pending_ws_close_reason{
    (uint32_t)WebSocketServer::CloseReason::unknown};
std::atomic<int32_t> pending_ws_transport_error{0};
absolute_time_t hello_retry;
absolute_time_t ping_retry;
absolute_time_t pong_deadline;
uint32_t ping_sequence;
uint8_t ping_payload[4];
bool awaiting_pong;
bool activity_since_ping;
uint8_t missed_pongs;
uint32_t session_network_generation;
bool network_online;

void increment_stat(uint32_t &counter, uint32_t amount = 1) {
  __atomic_fetch_add(&counter, amount, __ATOMIC_RELAXED);
}

void set_disconnect_reason(z80_web_terminal_disconnect_reason_t reason) {
  __atomic_store_n(&terminal_stats.last_disconnect_reason, (uint32_t)reason,
                   __ATOMIC_RELAXED);
}

const char *ws_close_reason_name(WebSocketServer::CloseReason reason) {
  switch (reason) {
  case WebSocketServer::CloseReason::peer_tcp_closed: return "peer-tcp-closed";
  case WebSocketServer::CloseReason::peer_websocket_close: return "peer-ws-close";
  case WebSocketServer::CloseReason::invalid_websocket_input: return "invalid-ws-input";
  case WebSocketServer::CloseReason::receive_queue_message_limit: return "receive-queue-message-limit";
  case WebSocketServer::CloseReason::receive_queue_byte_limit: return "receive-queue-byte-limit";
  case WebSocketServer::CloseReason::pong_backpressure: return "pong-backpressure";
  case WebSocketServer::CloseReason::tcp_write_error: return "tcp-write-error";
  case WebSocketServer::CloseReason::tcp_output_error: return "tcp-output-error";
  case WebSocketServer::CloseReason::tcp_error: return "tcp-error";
  case WebSocketServer::CloseReason::close_timeout: return "close-timeout";
  case WebSocketServer::CloseReason::server_abort: return "server-abort";
  case WebSocketServer::CloseReason::local_close: return "local-close";
  default: return "unknown";
  }
}

uint32_t ws_close_reason_code(WebSocketServer::CloseReason reason) {
  switch (reason) {
  case WebSocketServer::CloseReason::peer_tcp_closed: return Z80_WEB_TERMINAL_WS_CLOSE_PEER_TCP_CLOSED;
  case WebSocketServer::CloseReason::peer_websocket_close: return Z80_WEB_TERMINAL_WS_CLOSE_PEER_WEBSOCKET_CLOSE;
  case WebSocketServer::CloseReason::invalid_websocket_input: return Z80_WEB_TERMINAL_WS_CLOSE_INVALID_INPUT;
  case WebSocketServer::CloseReason::receive_queue_message_limit: return Z80_WEB_TERMINAL_WS_CLOSE_RECEIVE_QUEUE_MESSAGE_LIMIT;
  case WebSocketServer::CloseReason::receive_queue_byte_limit: return Z80_WEB_TERMINAL_WS_CLOSE_RECEIVE_QUEUE_BYTE_LIMIT;
  case WebSocketServer::CloseReason::pong_backpressure: return Z80_WEB_TERMINAL_WS_CLOSE_PONG_BACKPRESSURE;
  case WebSocketServer::CloseReason::tcp_write_error: return Z80_WEB_TERMINAL_WS_CLOSE_TCP_WRITE_ERROR;
  case WebSocketServer::CloseReason::tcp_output_error: return Z80_WEB_TERMINAL_WS_CLOSE_TCP_OUTPUT_ERROR;
  case WebSocketServer::CloseReason::tcp_error: return Z80_WEB_TERMINAL_WS_CLOSE_TCP_ERROR;
  case WebSocketServer::CloseReason::close_timeout: return Z80_WEB_TERMINAL_WS_CLOSE_TIMEOUT;
  case WebSocketServer::CloseReason::server_abort: return Z80_WEB_TERMINAL_WS_CLOSE_SERVER_ABORT;
  case WebSocketServer::CloseReason::local_close: return Z80_WEB_TERMINAL_WS_CLOSE_LOCAL_CLOSE;
  default: return Z80_WEB_TERMINAL_WS_CLOSE_UNKNOWN;
  }
}

void on_close_details(WebSocketServer &, uint32_t connection,
                      WebSocketServer::CloseReason reason, int transport_error,
                      size_t queued_messages, size_t queued_bytes,
                      size_t peak_queued_messages, size_t peak_queued_bytes,
                      size_t queue_message_limit, size_t queue_byte_limit) {
  int32_t active = active_connection.load(std::memory_order_acquire);
  if (active != (int32_t)connection &&
      last_disconnected_connection.load(std::memory_order_acquire) !=
          (int32_t)connection)
    return;
  last_disconnected_connection.store((int32_t)connection,
                                      std::memory_order_release);
  pending_ws_close_reason.store((uint32_t)reason, std::memory_order_release);
  pending_ws_transport_error.store(transport_error, std::memory_order_release);
  __atomic_store_n(&terminal_stats.last_ws_close_reason, ws_close_reason_code(reason),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_transport_error, transport_error,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_messages,
                   (uint32_t)queued_messages, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_bytes,
                   (uint32_t)queued_bytes, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_peak_messages,
                   (uint32_t)peak_queued_messages, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_peak_bytes,
                   (uint32_t)peak_queued_bytes, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_message_limit,
                   (uint32_t)queue_message_limit, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.last_ws_queue_byte_limit,
                   (uint32_t)queue_byte_limit, __ATOMIC_RELAXED);
  // A previous session may finish closing after its replacement connects.
  if (active >= 0 && active != (int32_t)connection)
    return;
  __atomic_store_n(&terminal_stats.rx_queue_messages,
                   active >= 0 ? (uint32_t)queued_messages : 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_bytes,
                   active >= 0 ? (uint32_t)queued_bytes : 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_peak_messages,
                   (uint32_t)peak_queued_messages, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_peak_bytes,
                   (uint32_t)peak_queued_bytes, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_message_limit,
                   (uint32_t)queue_message_limit, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_byte_limit,
                   (uint32_t)queue_byte_limit, __ATOMIC_RELAXED);
}

void record_peak(uint32_t &counter, uint32_t value) {
  if (value > __atomic_load_n(&counter, __ATOMIC_RELAXED))
    __atomic_store_n(&counter, value, __ATOMIC_RELAXED);
}

void publish_buffer_usage() {
  uint32_t frames = (uint32_t)output_count;
  uint32_t bytes = (uint32_t)(retained_output_bytes + staged_output_length);
  __atomic_store_n(&terminal_stats.window_frames, frames, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.retained_bytes, bytes, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.staged_bytes, (uint32_t)staged_output_length,
                   __ATOMIC_RELAXED);
  record_peak(terminal_stats.window_frames_peak, frames);
  record_peak(terminal_stats.retained_bytes_peak, bytes);
}

void reset_receive_queue_usage() {
  __atomic_store_n(&terminal_stats.rx_queue_messages, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_bytes, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_peak_messages, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_peak_bytes, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_message_limit, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_byte_limit, 0, __ATOMIC_RELAXED);
}

void clear_receive_queue_current() {
  __atomic_store_n(&terminal_stats.rx_queue_messages, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&terminal_stats.rx_queue_bytes, 0, __ATOMIC_RELAXED);
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
  protocol_ready = false;
  input_sequence = output_sequence = 1;
  output_head = output_count = pending_input_length = 0;
  input_ack_pending = false;
  staged_output_length = 0;
  retained_output_bytes = 0;
  publish_buffer_usage();
  output_batch_deadline = nil_time;
  hello_retry = nil_time;
  ping_retry = nil_time;
  awaiting_pong = false;
  activity_since_ping = false;
  missed_pongs = 0;
  pong_deadline = nil_time;
}

void note_browser_activity() {
  missed_pongs = 0;
  if (awaiting_pong)
    activity_since_ping = true;
}

void close_connection(WebSocketServer &websocket, uint32_t connection) {
  if (!websocket.close(connection))
    printf("[network] Close frame not sent; connection scheduled for cleanup.\n");
}

void disconnect_client(z80_web_terminal_disconnect_reason_t reason) {
  int32_t connection = active_connection.exchange(-1, std::memory_order_acq_rel);
  if (connection >= 0) {
    last_disconnected_connection.store(connection, std::memory_order_release);
    set_disconnect_reason(reason);
    // Do not pair this new disconnect with a previous session's close details.
    __atomic_store_n(&terminal_stats.last_ws_close_reason,
                     Z80_WEB_TERMINAL_WS_CLOSE_UNKNOWN, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_transport_error, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_messages, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_bytes, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_peak_messages, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_peak_bytes, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_message_limit, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&terminal_stats.last_ws_queue_byte_limit, 0, __ATOMIC_RELAXED);
    clear_receive_queue_current();
  }
  if (connection >= 0 && server != nullptr && !server->abort((uint32_t)connection))
    printf("[network] Terminal connection already removed during teardown.\n");
  pending_connected.store(false, std::memory_order_release);
  pending_disconnected.store(false, std::memory_order_release);
  z80_terminal_browser_disconnect();
  reset_session();
}

bool send_control(uint8_t type, uint32_t sequence) {
  uint8_t frame[HEADER_BYTES] = {type};
  write_sequence(frame + 1, sequence);
  return server->sendMessage((uint32_t)active_connection.load(), frame,
                             sizeof(frame));
}

void retry_input_ack() {
  if (input_ack_pending && send_control(INPUT_ACK, input_sequence - 1))
    input_ack_pending = false;
}

void accept_pending_input() {
  if (!pending_input_length ||
      !z80_terminal_browser_try_receive(pending_input, pending_input_length))
    return;
  pending_input_length = 0;
  ++input_sequence;
  input_ack_pending = true;
  retry_input_ack();
}

void on_connect(WebSocketServer &websocket, uint32_t connection) {
  int32_t expected = -1;
  if (active_connection.compare_exchange_strong(
          expected, (int32_t)connection, std::memory_order_acq_rel)) {
    reset_receive_queue_usage();
    pending_connected.store(true, std::memory_order_release);
  } else {
    close_connection(websocket, connection);
    printf("[network] Terminal busy; rejected another browser.\n");
  }
}

void on_close(WebSocketServer &, uint32_t connection) {
  int32_t expected = (int32_t)connection;
  if (active_connection.compare_exchange_strong(
          expected, -1, std::memory_order_acq_rel)) {
    last_disconnected_connection.store((int32_t)connection,
                                        std::memory_order_release);
    set_disconnect_reason(Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED);
    clear_receive_queue_current();
    pending_disconnected.store(true, std::memory_order_release);
  }
}

void on_message(WebSocketServer &, uint32_t connection,
                const void *data, size_t length) {
  if (active_connection.load(std::memory_order_acquire) != (int32_t)connection)
    return;
  const uint8_t *frame = static_cast<const uint8_t *>(data);
  if (length < HEADER_BYTES) {
    printf("[network] FAIL: invalid terminal protocol; reload the terminal page.\n");
    disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
    return;
  }
  uint32_t sequence = read_sequence(frame + 1);
  if (frame[0] == READY && length == HEADER_BYTES &&
      sequence == PROTOCOL_VERSION) {
    protocol_ready = true;
    if (ping_retry == nil_time)
      ping_retry = make_timeout_time_ms(HEARTBEAT_MS);
    note_browser_activity();
  } else if (frame[0] == OUTPUT_ACK && protocol_ready &&
             length == HEADER_BYTES) {
    note_browser_activity();
    for (size_t i = 0; i < output_count; ++i) {
      const OutputFrame &candidate = output_frames[(output_head + i) % OUTPUT_WINDOW];
      if (candidate.sent && read_sequence(candidate.data + 1) == sequence) {
        for (size_t j = 0; j <= i; ++j)
          retained_output_bytes -=
              output_frames[(output_head + j) % OUTPUT_WINDOW].length - HEADER_BYTES;
        output_head = (output_head + i + 1) % OUTPUT_WINDOW;
        output_count -= i + 1;
        publish_buffer_usage();
        break;
      }
    }
  } else if (frame[0] == INPUT && protocol_ready &&
             length > HEADER_BYTES && length <= HEADER_BYTES + INPUT_BYTES) {
    note_browser_activity();
    if (sequence == input_sequence) {
      size_t bytes = length - HEADER_BYTES;
      if (pending_input_length &&
          (pending_input_length != bytes ||
           memcmp(pending_input, frame + HEADER_BYTES, bytes) != 0)) {
        printf("[network] FAIL: changed pending terminal input; disconnecting.\n");
        disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
        return;
      }
      memcpy(pending_input, frame + HEADER_BYTES, bytes);
      pending_input_length = bytes;
      accept_pending_input();
    } else if (sequence == input_sequence - 1) {
      input_ack_pending = true;
      retry_input_ack();
    } else {
      printf("[network] FAIL: out-of-order terminal input; disconnecting.\n");
      disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
    }
  } else {
    printf("[network] FAIL: invalid terminal frame; reload the terminal page.\n");
    disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR);
  }
}

void on_pong(WebSocketServer &, uint32_t connection,
             const void *data, size_t length) {
  if (active_connection.load() != (int32_t)connection)
    return;
  if (awaiting_pong && length == sizeof(ping_payload) &&
      memcmp(data, ping_payload, length) == 0) {
    awaiting_pong = false;
    missed_pongs = 0;
    activity_since_ping = false;
    pong_deadline = nil_time;
  } else {
    // Even a late PONG proves the peer is responsive and can satisfy a
    // newer ping's liveness check through note_browser_activity().
    note_browser_activity();
  }
}

void process_events() {
  if (pending_disconnected.exchange(false, std::memory_order_acq_rel)) {
    auto reason = (WebSocketServer::CloseReason)pending_ws_close_reason.load(
        std::memory_order_acquire);
    int transport_error = pending_ws_transport_error.load(std::memory_order_acquire);
    printf("[network] WebSocket server closed connection: %s",
           ws_close_reason_name(reason));
    if (transport_error)
      printf(" (lwIP error=%d)", transport_error);
    printf("; queue=%lu/%lu messages, peak=%lu/%lu; bytes=%lu/%lu, peak=%lu/%lu.\n",
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_messages,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_message_limit,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_peak_messages,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_message_limit,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_bytes,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_byte_limit,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_peak_bytes,
                                          __ATOMIC_RELAXED),
           (unsigned long)__atomic_load_n(&terminal_stats.last_ws_queue_byte_limit,
                                          __ATOMIC_RELAXED));
    z80_terminal_browser_disconnect();
    reset_session();
  }
  if (pending_connected.exchange(false, std::memory_order_acq_rel) &&
      active_connection.load(std::memory_order_acquire) >= 0) {
    reset_session();
    z80_terminal_browser_connect();
  }
}

bool ensure_server() {
  if (server_started)
    return true;
  if (!time_reached(server_retry_deadline))
    return false;
  if (server == nullptr) {
    server = new (std::nothrow) WebSocketServer(SERVER_CONNECTIONS);
    if (server == nullptr) {
      printf("[network] FAIL: server allocation; retry in 5 seconds.\n");
      server_retry_deadline = make_timeout_time_ms(SERVER_RETRY_MS);
      return false;
    }
    server->setConnectCallback(on_connect);
    server->setCloseCallback(on_close);
    server->setCloseDetailsCallback(on_close_details);
    server->setMessageCallback(on_message);
    server->setPongCallback(on_pong);
    server->setTcpNoDelay(true);
    // ACK/PONG control messages are tiny but can arrive in a burst within one
    // CYW43 poll. Give the queue room to absorb that burst, then drain more of
    // it on each application poll; the byte cap still bounds payload storage.
    if (!server->setReceiveLimits({HEADER_BYTES + INPUT_BYTES, 1024, 64, 32})) {
      printf("[network] FAIL: terminal receive limits; retry in 5 seconds.\n");
      delete server;
      server = nullptr;
      server_retry_deadline = make_timeout_time_ms(SERVER_RETRY_MS);
      return false;
    }
  }
  server_started = server->startListening(SERVER_PORT);
  if (!server_started) {
    printf("[network] FAIL: HTTP/WebSocket listen; retry in 5 seconds.\n");
    server_retry_deadline = make_timeout_time_ms(SERVER_RETRY_MS);
  }
  return server_started;
}

void poll_output() {
  int32_t connection = active_connection.load(std::memory_order_acquire);
  if (connection < 0)
    return;
  if (output_count && time_reached(output_frames[output_head].deadline)) {
    increment_stat(terminal_stats.ack_timeouts);
    printf("[network] Terminal client timed out; restoring USB.\n");
    disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_ACK_TIMEOUT);
    return;
  }
  if (awaiting_pong && time_reached(pong_deadline)) {
    awaiting_pong = false;
    pong_deadline = nil_time;
    if (activity_since_ping) {
      missed_pongs = 0;
    } else {
      increment_stat(terminal_stats.pong_timeouts);
      ++missed_pongs;
      if (missed_pongs > MAX_MISSED_PONGS) {
        printf("[network] Terminal client missed too many PONGs; restoring USB.\n");
        disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_PONG_TIMEOUT);
        return;
      }
    }
    activity_since_ping = false;
    // Keep the 30-second send cadence; enqueue backpressure is retried on the
    // normal short retry cadence.
  }
  if (protocol_ready && !awaiting_pong && time_reached(ping_retry)) {
    write_sequence(ping_payload, ++ping_sequence);
    if (server->sendPing((uint32_t)connection, ping_payload,
                         sizeof(ping_payload))) {
      awaiting_pong = true;
      activity_since_ping = false;
      // A queued ping is outstanding until its matching PONG or the client
      // timeout. Do not retransmit it every poll; retry only failed enqueues.
      ping_retry = make_timeout_time_ms(HEARTBEAT_MS);
      pong_deadline = make_timeout_time_ms(PONG_TIMEOUT_MS);
    } else {
      ping_retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
  }
  if (!protocol_ready) {
    if (time_reached(hello_retry)) {
      send_control(HELLO, PROTOCOL_VERSION);
      hello_retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
    return;
  }
  bool output_changed = false;
  while (output_count < OUTPUT_WINDOW) {
    OutputFrame &frame = output_frames[(output_head + output_count) % OUTPUT_WINDOW];
    // ACKs advance head and reduce count together, preserving this staging slot.
    size_t supplied = z80_terminal_supply(
        frame.data + HEADER_BYTES + staged_output_length,
        FRAME_BYTES - staged_output_length);
    staged_output_length += supplied;
    output_changed |= supplied != 0;
    if (!staged_output_length)
      break;
    if (staged_output_length < FRAME_BYTES && !time_reached(output_batch_deadline))
      break;
    frame.data[0] = OUTPUT;
    write_sequence(frame.data + 1, output_sequence++);
    frame.length = HEADER_BYTES + staged_output_length;
    frame.retry = nil_time;
    frame.deadline = make_timeout_time_ms(OUTPUT_ACK_TIMEOUT_MS);
    frame.sent = false;
    retained_output_bytes += staged_output_length;
    ++output_count;
    output_changed = true;
    staged_output_length = 0;
    output_batch_deadline = make_timeout_time_us(OUTPUT_COALESCE_US);
  }
  if (output_changed)
    publish_buffer_usage();
  bool queued = false;
  for (size_t i = 0; i < output_count; ++i) {
    OutputFrame &frame = output_frames[(output_head + i) % OUTPUT_WINDOW];
    if (time_reached(frame.retry)) {
      auto result = server->enqueueMessage((uint32_t)connection, frame.data, frame.length);
      if (result == WebSocketServer::SendResult::would_block)
        break;
      if (result != WebSocketServer::SendResult::queued) {
        printf("[network] FAIL: terminal output enqueue (%u); restoring USB.\n",
               (unsigned)result);
        disconnect_client(Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR);
        return;
      }
      queued = true;
      if (frame.sent)
        increment_stat(terminal_stats.retransmits);
      frame.sent = true;
      frame.retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
    if (!frame.sent)
      break;
  }
  if (queued && !server->flush((uint32_t)connection))
    increment_stat(terminal_stats.flush_failures);
}
}  // namespace

extern "C" z80_web_terminal_stats_t z80_web_terminal_get_stats(void) {
  z80_web_terminal_stats_t stats = {};
  stats.retransmits =
      __atomic_load_n(&terminal_stats.retransmits, __ATOMIC_RELAXED);
  stats.flush_failures =
      __atomic_load_n(&terminal_stats.flush_failures, __ATOMIC_RELAXED);
  stats.ack_timeouts =
      __atomic_load_n(&terminal_stats.ack_timeouts, __ATOMIC_RELAXED);
  stats.pong_timeouts =
      __atomic_load_n(&terminal_stats.pong_timeouts, __ATOMIC_RELAXED);
  stats.window_frames =
      __atomic_load_n(&terminal_stats.window_frames, __ATOMIC_RELAXED);
  stats.window_frames_peak =
      __atomic_load_n(&terminal_stats.window_frames_peak, __ATOMIC_RELAXED);
  stats.retained_bytes =
      __atomic_load_n(&terminal_stats.retained_bytes, __ATOMIC_RELAXED);
  stats.retained_bytes_peak =
      __atomic_load_n(&terminal_stats.retained_bytes_peak, __ATOMIC_RELAXED);
  stats.staged_bytes =
      __atomic_load_n(&terminal_stats.staged_bytes, __ATOMIC_RELAXED);
  stats.last_disconnect_reason =
      __atomic_load_n(&terminal_stats.last_disconnect_reason, __ATOMIC_RELAXED);
  stats.last_ws_close_reason =
      __atomic_load_n(&terminal_stats.last_ws_close_reason, __ATOMIC_RELAXED);
  stats.last_ws_transport_error =
      __atomic_load_n(&terminal_stats.last_ws_transport_error, __ATOMIC_RELAXED);
  stats.last_ws_queue_messages =
      __atomic_load_n(&terminal_stats.last_ws_queue_messages, __ATOMIC_RELAXED);
  stats.last_ws_queue_bytes =
      __atomic_load_n(&terminal_stats.last_ws_queue_bytes, __ATOMIC_RELAXED);
  stats.last_ws_queue_peak_messages =
      __atomic_load_n(&terminal_stats.last_ws_queue_peak_messages,
                      __ATOMIC_RELAXED);
  stats.last_ws_queue_peak_bytes =
      __atomic_load_n(&terminal_stats.last_ws_queue_peak_bytes, __ATOMIC_RELAXED);
  stats.last_ws_queue_message_limit =
      __atomic_load_n(&terminal_stats.last_ws_queue_message_limit,
                      __ATOMIC_RELAXED);
  stats.last_ws_queue_byte_limit =
      __atomic_load_n(&terminal_stats.last_ws_queue_byte_limit, __ATOMIC_RELAXED);
  stats.rx_queue_messages =
      __atomic_load_n(&terminal_stats.rx_queue_messages, __ATOMIC_RELAXED);
  stats.rx_queue_bytes =
      __atomic_load_n(&terminal_stats.rx_queue_bytes, __ATOMIC_RELAXED);
  stats.rx_queue_peak_messages =
      __atomic_load_n(&terminal_stats.rx_queue_peak_messages, __ATOMIC_RELAXED);
  stats.rx_queue_peak_bytes =
      __atomic_load_n(&terminal_stats.rx_queue_peak_bytes, __ATOMIC_RELAXED);
  stats.rx_queue_message_limit =
      __atomic_load_n(&terminal_stats.rx_queue_message_limit, __ATOMIC_RELAXED);
  stats.rx_queue_byte_limit =
      __atomic_load_n(&terminal_stats.rx_queue_byte_limit, __ATOMIC_RELAXED);

  int32_t connection = active_connection.load(std::memory_order_acquire);
  if (server != nullptr && connection >= 0) {
    WebSocketServer::ReceiveQueueStats queue;
    if (server->getReceiveQueueStats((uint32_t)connection, queue)) {
      stats.rx_queue_active = 1;
      stats.rx_queue_messages = (uint32_t)queue.queuedMessages;
      stats.rx_queue_bytes = (uint32_t)queue.queuedBytes;
      stats.rx_queue_peak_messages = (uint32_t)queue.peakQueuedMessages;
      stats.rx_queue_peak_bytes = (uint32_t)queue.peakQueuedBytes;
      stats.rx_queue_message_limit = (uint32_t)queue.messageLimit;
      stats.rx_queue_byte_limit = (uint32_t)queue.byteLimit;
      __atomic_store_n(&terminal_stats.rx_queue_messages,
                       stats.rx_queue_messages, __ATOMIC_RELAXED);
      __atomic_store_n(&terminal_stats.rx_queue_bytes,
                       stats.rx_queue_bytes, __ATOMIC_RELAXED);
      __atomic_store_n(&terminal_stats.rx_queue_peak_messages,
                       stats.rx_queue_peak_messages, __ATOMIC_RELAXED);
      __atomic_store_n(&terminal_stats.rx_queue_peak_bytes,
                       stats.rx_queue_peak_bytes, __ATOMIC_RELAXED);
      __atomic_store_n(&terminal_stats.rx_queue_message_limit,
                       stats.rx_queue_message_limit, __ATOMIC_RELAXED);
      __atomic_store_n(&terminal_stats.rx_queue_byte_limit,
                       stats.rx_queue_byte_limit, __ATOMIC_RELAXED);
    }
  }
  return stats;
}

extern "C" void z80_web_terminal_poll(bool online, uint32_t generation) {
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
  if (online && ensure_server()) {
    process_events();
    server->popMessages();
    process_events();
    accept_pending_input();
    retry_input_ack();
    poll_output();
  } else if (server != nullptr) {
    server->popMessages();
    process_events();
  }
  cyw43_arch_lwip_end();
}
