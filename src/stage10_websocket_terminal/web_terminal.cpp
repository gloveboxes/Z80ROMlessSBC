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
constexpr uint16_t SERVER_PORT = 8088;
constexpr uint32_t SERVER_CONNECTIONS = 4;
constexpr uint32_t SERVER_RETRY_MS = 5000;
constexpr size_t FRAME_BYTES = 256;
constexpr size_t INPUT_BYTES = 64;
constexpr size_t HEADER_BYTES = 5;
constexpr size_t OUTPUT_WINDOW = 4;
constexpr uint32_t PROTOCOL_VERSION = 2;
constexpr uint32_t FRAME_RETRY_MS = 250;
constexpr uint32_t HEARTBEAT_MS = 5000;
constexpr uint32_t CLIENT_TIMEOUT_MS = 15000;
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
uint8_t pending_input[INPUT_BYTES];
size_t pending_input_length;
uint32_t output_sequence;
uint32_t input_sequence;
bool protocol_ready;
WebSocketServer *server;
bool server_started;
absolute_time_t server_retry_deadline = nil_time;
std::atomic<int32_t> active_connection{-1};
std::atomic<bool> pending_connected{false};
std::atomic<bool> pending_disconnected{false};
absolute_time_t hello_retry;
absolute_time_t client_deadline;
absolute_time_t ping_retry;
uint32_t ping_sequence;
uint8_t ping_payload[4];
bool awaiting_pong;
uint32_t session_network_generation;
bool network_online;
char announced_address[16];
absolute_time_t announce_deadline = nil_time;

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
  hello_retry = ping_retry = nil_time;
  client_deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
  awaiting_pong = false;
}

void close_connection(WebSocketServer &websocket, uint32_t connection) {
  if (!websocket.close(connection))
    printf("[network] Close frame not sent; connection scheduled for cleanup.\n");
}

void disconnect_client() {
  int32_t connection = active_connection.exchange(-1, std::memory_order_acq_rel);
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

void accept_pending_input() {
  if (!pending_input_length ||
      !z80_terminal_browser_try_receive(pending_input, pending_input_length))
    return;
  pending_input_length = 0;
  send_control(INPUT_ACK, input_sequence++);
}

void on_connect(WebSocketServer &websocket, uint32_t connection) {
  int32_t expected = -1;
  if (active_connection.compare_exchange_strong(
          expected, (int32_t)connection, std::memory_order_acq_rel)) {
    pending_connected.store(true, std::memory_order_release);
  } else {
    close_connection(websocket, connection);
    printf("[network] Terminal busy; rejected another browser.\n");
  }
}

void on_close(WebSocketServer &, uint32_t connection) {
  int32_t expected = (int32_t)connection;
  if (active_connection.compare_exchange_strong(
          expected, -1, std::memory_order_acq_rel))
    pending_disconnected.store(true, std::memory_order_release);
}

void on_message(WebSocketServer &, uint32_t connection,
                const void *data, size_t length) {
  if (active_connection.load(std::memory_order_acquire) != (int32_t)connection)
    return;
  const uint8_t *frame = static_cast<const uint8_t *>(data);
  if (length < HEADER_BYTES) {
    printf("[network] FAIL: invalid terminal protocol; reload the terminal page.\n");
    disconnect_client();
    return;
  }
  uint32_t sequence = read_sequence(frame + 1);
  if (frame[0] == READY && length == HEADER_BYTES &&
      sequence == PROTOCOL_VERSION) {
    protocol_ready = true;
  } else if (frame[0] == OUTPUT_ACK && protocol_ready &&
             length == HEADER_BYTES) {
    for (size_t i = 0; i < output_count; ++i) {
      const OutputFrame &candidate = output_frames[(output_head + i) % OUTPUT_WINDOW];
      if (candidate.sent && read_sequence(candidate.data + 1) == sequence) {
        output_head = (output_head + i + 1) % OUTPUT_WINDOW;
        output_count -= i + 1;
        break;
      }
    }
  } else if (frame[0] == INPUT && protocol_ready &&
             length > HEADER_BYTES && length <= HEADER_BYTES + INPUT_BYTES) {
    if (sequence == input_sequence) {
      size_t bytes = length - HEADER_BYTES;
      if (pending_input_length &&
          (pending_input_length != bytes ||
           memcmp(pending_input, frame + HEADER_BYTES, bytes) != 0)) {
        printf("[network] FAIL: changed pending terminal input; disconnecting.\n");
        disconnect_client();
        return;
      }
      memcpy(pending_input, frame + HEADER_BYTES, bytes);
      pending_input_length = bytes;
      accept_pending_input();
    } else if (sequence == input_sequence - 1) {
      send_control(INPUT_ACK, sequence);
    } else {
      printf("[network] FAIL: out-of-order terminal input; disconnecting.\n");
      disconnect_client();
    }
  } else {
    printf("[network] FAIL: invalid terminal frame; reload the terminal page.\n");
    disconnect_client();
  }
}

void on_pong(WebSocketServer &, uint32_t connection,
             const void *data, size_t length) {
  if (active_connection.load() == (int32_t)connection && awaiting_pong &&
      length == sizeof(ping_payload) &&
      memcmp(data, ping_payload, length) == 0) {
    awaiting_pong = false;
    client_deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
    ping_retry = make_timeout_time_ms(HEARTBEAT_MS);
  }
}

void process_events() {
  if (pending_disconnected.exchange(false, std::memory_order_acq_rel)) {
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
    server->setMessageCallback(on_message);
    server->setPongCallback(on_pong);
    server->setTcpNoDelay(true);
    if (!server->setReceiveLimits({HEADER_BYTES + INPUT_BYTES, 1024, 16, 8})) {
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
  if (time_reached(client_deadline) ||
      (output_count && time_reached(output_frames[output_head].deadline))) {
    printf("[network] Terminal client timed out; restoring USB.\n");
    disconnect_client();
    return;
  }
  if (time_reached(ping_retry)) {
    if (!awaiting_pong) {
      write_sequence(ping_payload, ++ping_sequence);
      awaiting_pong = true;
    }
    server->sendPing((uint32_t)connection, ping_payload, sizeof(ping_payload));
    ping_retry = make_timeout_time_ms(FRAME_RETRY_MS);
  }
  if (!protocol_ready) {
    if (time_reached(hello_retry)) {
      send_control(HELLO, PROTOCOL_VERSION);
      hello_retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
    return;
  }
  while (output_count < OUTPUT_WINDOW) {
    OutputFrame &frame = output_frames[(output_head + output_count) % OUTPUT_WINDOW];
    size_t length = z80_terminal_supply(frame.data + HEADER_BYTES, FRAME_BYTES);
    if (!length)
      break;
    frame.data[0] = OUTPUT;
    write_sequence(frame.data + 1, output_sequence++);
    frame.length = HEADER_BYTES + length;
    frame.retry = nil_time;
    frame.deadline = make_timeout_time_ms(CLIENT_TIMEOUT_MS);
    frame.sent = false;
    ++output_count;
  }
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
        disconnect_client();
        return;
      }
      queued = true;
      frame.sent = true;
      frame.retry = make_timeout_time_ms(FRAME_RETRY_MS);
    }
    if (!frame.sent)
      break;
  }
  if (queued)
    server->flush((uint32_t)connection);
}
}  // namespace

extern "C" void z80_web_terminal_poll(bool online, uint32_t generation) {
  if (!online && !network_online && generation == session_network_generation)
    return;
  if (!online && server == nullptr) {
    session_network_generation = generation;
    return;
  }
  cyw43_arch_lwip_begin();
  if (!online || generation != session_network_generation) {
    disconnect_client();
    session_network_generation = generation;
    announced_address[0] = '\0';
    announce_deadline = nil_time;
  }
  network_online = online;
  if (online && ensure_server()) {
    if (time_reached(announce_deadline)) {
      announce_deadline = make_timeout_time_ms(1000);
      char address[16];
      z80_io_ports_get_network_address(address, sizeof(address));
      if (strcmp(address, announced_address) != 0) {
        strcpy(announced_address, address);
        if (address[0])
          printf("[network] Terminal: http://%s:%u/\n", address, SERVER_PORT);
      }
    }
    process_events();
    server->popMessages();
    process_events();
    accept_pending_input();
    poll_output();
  } else if (server != nullptr) {
    server->popMessages();
    process_events();
  }
  cyw43_arch_lwip_end();
}
