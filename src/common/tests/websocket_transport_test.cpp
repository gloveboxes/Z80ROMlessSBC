#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "cyw43_config.h"
#include "lwip/sys.h"
#include "pico_ws_server/cyw43_guard.h"
#include "pico_ws_server/web_socket_server.h"
#include "lwip/tcp.h"

// Count every heap allocation so the steady-state paths can prove they make none.
static unsigned long allocations;
static void* counted(size_t size) {
  ++allocations;
  return malloc(size ? size : 1);
}
void* operator new(size_t size) {
  void* memory = counted(size);
  assert(memory);
  return memory;
}
void* operator new[](size_t size) {
  void* memory = counted(size);
  assert(memory);
  return memory;
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* memory) noexcept { free(memory); }
void operator delete[](void* memory) noexcept { free(memory); }
void operator delete(void* memory, size_t) noexcept { free(memory); }
void operator delete[](void* memory, size_t) noexcept { free(memory); }

using Result = WebSocketServer::SendResult;
using CloseReason = WebSocketServer::CloseReason;
static constexpr char handshake[] =
    "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";

struct Fixture {
  struct CloseDetails {
    uint32_t id;
    CloseReason reason;
    int transport_error;
  };
  // PCBs must outlive the server destructor.
  tcp_pcb first, second, replacement;
  WebSocketServer server{2};
  std::vector<uint32_t> ids, closed, delivered_ids;
  std::vector<std::vector<uint8_t>> messages, pongs;
  std::vector<CloseDetails> close_details;
  bool abort_on_message = false, abort_on_connect = false, abort_on_pong = false;
  bool close_on_message = false, reply_on_message = false;
  bool reenter_poll = false;
  // Aborted from the next message callback, whichever connection receives it.
  uint32_t abort_other = 0;

  Fixture() {
    test_now = 0;
    server.setCallbackExtra(this);
    server.setTcpNoDelay(true);
    server.setConnectCallback([](WebSocketServer& s, uint32_t id) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      f.ids.push_back(id);
      if (f.abort_on_connect)
        assert(s.abort(id));
      if (f.reenter_poll)
        s.poll();
    });
    server.setCloseCallback([](WebSocketServer& s, uint32_t id, CloseReason reason,
                               int transport_error) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      f.closed.push_back(id);
      f.close_details.push_back({id, reason, transport_error});
      // A teardown notification must not resurrect a dying connection.
      assert(s.enqueueMessage(id, "x", 1) == Result::closed);
      s.poll();
    });
    server.setMessageCallback([](WebSocketServer& s, uint32_t id, const void* data, size_t len) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      auto bytes = static_cast<const uint8_t*>(data);
      assert(bytes[len] == 0);
      f.messages.emplace_back(bytes, bytes + len);
      f.delivered_ids.push_back(id);
      if (f.reply_on_message)
        assert(s.sendMessage(id, bytes, len));
      if (f.reenter_poll)
        s.poll();
      if (f.abort_other)
        assert(s.abort(f.abort_other));
      if (f.close_on_message)
        assert(s.close(id));
      if (f.abort_on_message)
        assert(s.abort(id));
    });
    server.setPongCallback([](WebSocketServer& s, uint32_t id, const void* data, size_t len) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      auto bytes = static_cast<const uint8_t*>(data);
      f.pongs.emplace_back(bytes, bytes + len);
      if (f.abort_on_pong)
        assert(s.abort(id));
    });
  }

  void start() { assert(server.startListening(8080)); }
  void accept(tcp_pcb& pcb) {
    Cyw43Guard guard;
    assert(test_listener.accept(test_listener.arg, &pcb, ERR_OK) == ERR_OK);
    assert(pcb.nodelay && pcb.poll_interval == 1);
  }
  err_t feed(tcp_pcb& pcb, std::vector<uint8_t> bytes, size_t span_size = 1) {
    assert(!bytes.empty() && bytes.size() <= UINT16_MAX && pcb.recv);
    size_t count = (bytes.size() + span_size - 1) / span_size;
    std::vector<pbuf> spans(count);
    for (size_t i = 0, offset = 0; i < count; ++i) {
      size_t length = std::min(span_size, bytes.size() - offset);
      spans[i] = {i + 1 < count ? &spans[i + 1] : nullptr,
                  bytes.data() + offset, static_cast<u16_t>(bytes.size() - offset),
                  static_cast<u16_t>(length)};
      offset += length;
    }
    Cyw43Guard guard;
    unsigned frees = pbuf_frees;
    err_t result = pcb.recv(pcb.arg, &pcb, spans.data(), ERR_OK);
    assert(pbuf_frees == frees + 1);
    return result;
  }
  void upgrade(tcp_pcb& pcb) {
    accept(pcb);
    assert(feed(pcb, {handshake, handshake + strlen(handshake)}) == ERR_OK);
    assert(!ids.empty());
    std::string response;
    for (auto& bytes : pcb.writes)
      response.append(bytes.begin(), bytes.end());
    assert(response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    resetSend(pcb);
  }
  static void resetSend(tcp_pcb& pcb) {
    pcb.writes.clear();
    pcb.outputs = 0;
    pcb.available = TCP_SND_BUF;
  }
  static err_t poll(tcp_pcb& pcb) {
    Cyw43Guard guard;
    return pcb.poll(pcb.arg, &pcb);
  }
  static err_t sent(tcp_pcb& pcb, u16_t bytes = 1) {
    Cyw43Guard guard;
    pcb.available = TCP_SND_BUF;
    return pcb.sent(pcb.arg, &pcb, bytes);
  }
};

static std::vector<uint8_t> frame(uint8_t opcode, const std::vector<uint8_t>& payload = {},
                                  bool final = true) {
  std::vector<uint8_t> bytes{static_cast<uint8_t>((final ? 0x80 : 0) | opcode)};
  if (payload.size() <= 125) {
    bytes.push_back(static_cast<uint8_t>(0x80 | payload.size()));
  } else {
    bytes.insert(bytes.end(), {0xfe, static_cast<uint8_t>(payload.size() >> 8),
                              static_cast<uint8_t>(payload.size())});
  }
  constexpr uint8_t mask[] = {0x37, 0xfa, 0x21, 0x3d};
  bytes.insert(bytes.end(), std::begin(mask), std::end(mask));
  for (size_t i = 0; i < payload.size(); ++i)
    bytes.push_back(payload[i] ^ mask[i & 3]);
  return bytes;
}

static void append(std::vector<uint8_t>& to, const std::vector<uint8_t>& from) {
  to.insert(to.end(), from.begin(), from.end());
}

static void test_send() {
  Fixture f;
  f.start();
  f.upgrade(f.first);
  uint32_t id = f.ids.back();
  std::vector<uint8_t> payload(517, 'X');
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = (uint8_t)i;
  for (unsigned i = 0; i < 4; ++i)
    assert(f.server.enqueueMessage(id, payload.data(), payload.size()) == Result::queued);
  assert(f.first.writes.size() == 4 && f.first.outputs == 0);
  for (auto& bytes : f.first.writes)
    assert(bytes.size() == 521 && bytes[0] == 0x82 && bytes[1] == 126 &&
           bytes[2] == 2 && bytes[3] == 5 &&
           std::vector<uint8_t>(bytes.begin() + 4, bytes.end()) == payload);
  assert(f.server.flush(id) && f.first.outputs == 1);
  assert(f.server.flush(id) && f.first.outputs == 1);
  f.first.available = 520;
  assert(f.server.enqueueMessage(id, payload.data(), payload.size()) == Result::would_block);
  assert(f.first.writes.size() == 4);
  assert(Fixture::sent(f.first) == ERR_OK);
  f.first.write_result = ERR_MEM;
  assert(f.server.enqueueMessage(id, "a", 1) == Result::would_block);
  assert(f.first.writes.size() == 4);
  f.first.write_result = ERR_OK;
  assert(f.server.sendMessage(id, "a", 1));
  assert(f.first.writes.size() == 5 && f.first.outputs == 2);
  assert(f.server.sendPing(id));
  assert(f.first.writes.back() == std::vector<uint8_t>({0x89, 0}));
  assert(f.first.outputs == 3);
  assert(f.server.enqueueMessage(id, nullptr, 1) == Result::invalid);
  assert(f.server.enqueueMessage(id, payload.data(), 64001) == Result::too_large);
  payload.resize(TCP_SND_BUF);
  assert(f.server.enqueueMessage(id, payload.data(), payload.size()) == Result::too_large);
  assert(f.server.enqueueMessage(id + 99, "x", 1) == Result::closed);
  assert(!f.server.sendPing(id, payload.data(), 126));
  f.server.setTcpNoDelay(false);
  assert(!f.first.nodelay);
  f.server.setTcpNoDelay(true);
  assert(f.first.nodelay);
}

static void test_flush() {
  for (err_t failure : {ERR_MEM, ERR_BUF, ERR_RTE}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    uint32_t id = f.ids.back();
    f.first.output_result = failure;
    std::vector<uint8_t> payload(517, 'A');
    assert(f.server.sendMessage(id, payload.data(), payload.size()));
    assert(f.first.writes.size() == 1 && f.first.outputs == 1);
    assert(Fixture::sent(f.first) == ERR_OK);
    assert(f.first.writes.size() == 1 && f.first.outputs == 2);
    f.first.output_result = ERR_OK;
    assert(Fixture::poll(f.first) == ERR_OK);
    assert(f.first.writes.size() == 1 && f.first.outputs == 3);
    f.server.poll();
    assert(f.first.outputs == 3 && f.first.writes.size() == 1);
  }
  for (unsigned recovery = 0; recovery < 3; ++recovery) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    f.first.write_result = ERR_MEM;
    auto ping = frame(9, {'a', 'b'});
    assert(f.feed(f.first, ping) == ERR_OK);
    assert(f.first.writes.empty() && !f.first.aborted);
    f.first.write_result = ERR_OK;
    f.first.output_result = ERR_MEM;
    if (recovery == 0)
      f.server.poll();
    else if (recovery == 1)
      assert(Fixture::sent(f.first) == ERR_OK);
    else
      assert(Fixture::poll(f.first) == ERR_OK);
    assert(f.first.writes == std::vector<std::vector<uint8_t>>({{0x8a, 2, 'a', 'b'}}));
    f.first.output_result = ERR_OK;
    f.server.poll();
    assert(f.first.writes.size() == 1 && !f.first.aborted);
  }
}

static void test_receive() {
  Fixture f;
  f.start();
  f.upgrade(f.first);
  std::vector<uint8_t> payload(261);
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(i);
  auto input = frame(2, payload);
  append(input, frame(2));
  append(input, frame(1, {'h', 'i'}));
  f.first.flags = 0;
  // Each message reaches the application as its last byte arrives.
  for (size_t i = 0; i + 1 < input.size(); ++i)
    assert(f.feed(f.first, {input[i]}) == ERR_OK);
  assert(f.messages.size() == 2);
  assert(f.feed(f.first, {input.back()}) == ERR_OK);
  // Received data requests an immediate ACK instead of waiting for the lwIP
  // fast timer, so interactive keystrokes are not held behind a delayed ACK.
  assert(f.first.flags & TF_ACK_NOW);
  assert(f.messages.size() == 3 && f.messages[0] == payload &&
         f.messages[1].empty() && f.messages[2] == std::vector<uint8_t>({'h', 'i'}));

  // Control frames may interleave with the fragments of one message.
  input = frame(1, {'a'}, false);
  append(input, frame(9));
  append(input, frame(10, {'p'}));
  append(input, frame(0, {}, false));
  append(input, frame(0, {'b'}));
  assert(f.feed(f.first, input, 3) == ERR_OK);
  assert(f.first.writes.back() == std::vector<uint8_t>({0x8a, 0}));
  assert(f.messages.back() == std::vector<uint8_t>({'a', 'b'}));
  assert(f.pongs == std::vector<std::vector<uint8_t>>({{'p'}}));

  // A burst in one segment is delivered in order, and a reply written by the
  // callback is queued before the receive callback returns.
  Fixture::resetSend(f.first);
  f.messages.clear();
  f.reply_on_message = true;
  input.clear();
  for (uint8_t i = 0; i < 40; ++i)
    append(input, frame(2, {i}));
  assert(f.feed(f.first, input, 13) == ERR_OK);
  assert(f.messages.size() == 40 && f.first.writes.size() == 40);
  for (uint8_t i = 0; i < 40; ++i)
    assert(f.messages[i] == std::vector<uint8_t>{i} &&
           f.first.writes[i] == std::vector<uint8_t>({0x82, 1, i}));
  // lwIP sends those replies when the receive callback returns, so no output
  // is requested inside it. The next poll confirms the flush, retrying while
  // output is blocked and not at all afterwards.
  assert(f.first.outputs == 0);
  f.first.output_result = ERR_MEM;
  f.server.poll();
  f.first.output_result = ERR_OK;
  f.server.poll();
  f.server.poll();
  assert(f.first.outputs == 2 && f.first.writes.size() == 40);
}

static void test_limits() {
  Fixture f;
  assert(!f.server.setMaxMessageSize(0));
  assert(!f.server.setMaxMessageSize(64001));
  assert(f.server.setMaxMessageSize(64));
  f.start();
  assert(!f.server.setMaxMessageSize(64));
  f.upgrade(f.first);
  f.upgrade(f.second);
  // Both slots are in use: another connection is refused, not queued.
  {
    Cyw43Guard guard;
    assert(test_listener.accept(test_listener.arg, &f.replacement, ERR_OK) == ERR_ABRT);
    assert(f.replacement.aborted);
    f.replacement = tcp_pcb{};
  }
  assert(f.feed(f.first, frame(2, std::vector<uint8_t>(64, 'x'))) == ERR_OK);
  assert(f.messages.back().size() == 64);
  assert(f.feed(f.first, frame(2, std::vector<uint8_t>(65, 'x'))) == ERR_OK);
  assert(f.first.closed && f.closed.size() == 1 && f.messages.size() == 1);
  assert(f.close_details.back().reason == CloseReason::invalid_websocket_input);
  // The limit covers a whole fragmented message.
  auto input = frame(2, std::vector<uint8_t>(40, 'x'), false);
  append(input, frame(0, std::vector<uint8_t>(24, 'y')));
  assert(f.feed(f.second, input) == ERR_OK);
  assert(f.messages.back().size() == 64 && !f.second.closed);
  input = frame(2, std::vector<uint8_t>(40, 'x'), false);
  append(input, frame(0, std::vector<uint8_t>(25, 'y')));
  assert(f.feed(f.second, input) == ERR_OK);
  assert(f.second.closed && f.closed.size() == 2 && f.messages.size() == 2);
  // The freed slot is reusable.
  f.upgrade(f.replacement);
  assert(f.feed(f.replacement, frame(2, {'n'})) == ERR_OK);
  assert(f.messages.back() == std::vector<uint8_t>{'n'});
}

static void test_invalid() {
  std::vector<std::vector<uint8_t>> invalid = {
      {0x82, 0},                  // unmasked
      {0xc2, 0x80, 0, 0, 0, 0}, // reserved bit
      frame(3), frame(0), frame(9, {}, false),
      frame(9, std::vector<uint8_t>(126)), frame(8, {'x'}),
      {0x82, 0xfe, 0, 1, 0, 0, 0, 0}, // nonminimal length
      {0x82, 0xff, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
  };
  auto interrupted = frame(1, {'x'}, false);
  append(interrupted, frame(2, {'y'}));
  invalid.push_back(interrupted);
  auto too_many = frame(1, {}, false);
  for (unsigned i = 0; i < 1024; ++i)
    append(too_many, frame(0, {}, false));
  invalid.push_back(too_many);
  for (auto& input : invalid) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    f.first.close_result = ERR_MEM;
    assert(f.feed(f.first, input, 17) == ERR_ABRT);
    assert(f.first.aborted && f.closed.size() == 1 && f.messages.empty());
    assert(f.close_details.back().reason == CloseReason::invalid_websocket_input);
    f.server.poll();
    assert(f.closed.size() == 1);
  }
}

static void test_lifecycle() {
  {
    Fixture f;
    f.server.setConnectCallback([](WebSocketServer& s, uint32_t id) {
      auto& fixture = *static_cast<Fixture*>(s.getCallbackExtra());
      if (!fixture.ids.empty())
        assert(s.abort(fixture.ids.back()));
      fixture.ids.push_back(id);
    });
    f.start();
    f.upgrade(f.first);
    f.upgrade(f.second);
    // Another connection's abort waits for poll(), never a nested teardown.
    assert(!f.first.aborted && !f.second.aborted && f.closed.empty());
    f.server.poll();
    assert(f.first.aborted && !f.first.arg && !f.first.recv &&
           !f.first.sent && !f.first.poll && !f.first.error);
    assert(!f.second.aborted && f.closed.size() == 1 && f.messages.empty());
    assert(f.close_details.back().reason == CloseReason::server_abort);
    // Reclaim each displaced slot in one pass, even if the peer never replies.
    tcp_pcb* current = &f.second;
    tcp_pcb* next = &f.first;
    for (unsigned i = 0; i < 12; ++i) {
      *next = tcp_pcb{};
      f.upgrade(*next);
      f.server.poll();
      assert(current->aborted && !next->aborted && f.closed.size() == i + 2);
      assert(f.feed(*next, frame(2, {'n', 'e', 'w'})) == ERR_OK);
      assert(f.delivered_ids.back() == f.ids.back());
      std::swap(current, next);
    }
  }
  for (bool write_failure : {false, true}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    uint32_t id = f.ids.back();
    f.first.write_result = write_failure ? ERR_MEM : ERR_OK;
    f.first.output_result = ERR_MEM;
    test_now = UINT32_MAX - 499;
    assert(f.server.close(id) == !write_failure);
    assert(f.server.enqueueMessage(id, "x", 1) == Result::closed);
    f.server.poll();
    assert(!f.first.aborted);
    test_now += 999;
    assert(Fixture::poll(f.first) == ERR_OK);
    test_now += 1;
    assert(Fixture::poll(f.first) == ERR_ABRT);
    assert(f.first.aborted && f.closed.size() == 1 && !f.first.error && !f.first.poll);
    assert(f.close_details.back().reason == CloseReason::close_timeout);
    f.upgrade(f.replacement);
    assert(f.ids.back() != id);
  }
  // A callback may abort its own connection: teardown happens once, when the
  // receive callback returns, and later frames in the segment are dropped.
  // The later frame may share the span or arrive in the next one.
  for (unsigned mode = 0; mode < 6; ++mode) {
    const size_t span_size = mode < 3 ? 1 : 64;
    Fixture f;
    f.start();
    f.abort_on_connect = mode % 3 == 0;
    f.abort_on_message = mode % 3 == 1;
    f.abort_on_pong = mode % 3 == 2;
    f.reenter_poll = true;
    if (mode % 3 == 0) {
      f.accept(f.first);
      // Aborting from the connect callback, with and without a frame after
      // the upgrade request.
      std::vector<uint8_t> input(handshake, handshake + strlen(handshake));
      if (mode)
        append(input, frame(2, {'n'}));
      assert(f.feed(f.first, input, input.size()) == ERR_ABRT);
    } else {
      f.upgrade(f.first);
      auto input = frame(mode % 3 == 1 ? 2 : 10, {'x'});
      append(input, frame(2, {'n'}));
      assert(f.feed(f.first, input, span_size) == ERR_ABRT);
    }
    assert(f.first.aborted && !f.first.arg && !f.first.recv && f.closed.size() == 1);
    assert(f.close_details.back().reason == CloseReason::server_abort);
    assert(f.messages.size() == (mode % 3 == 1 ? 1 : 0));
    assert(!f.server.abort(f.ids.back()));
    f.server.poll();
    assert(f.closed.size() == 1);
  }
  // A callback may close its own connection: the close frame is queued, later
  // frames are dropped and the TCP connection closes without a grace wait.
  for (size_t span_size : {1, 64}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    f.close_on_message = true;
    auto input = frame(2, {'x'});
    append(input, frame(2, {'n'}));
    assert(f.feed(f.first, input, span_size) == ERR_OK);
    assert(f.first.closed && !f.first.aborted && f.closed.size() == 1);
    assert(f.messages.size() == 1);
    assert(f.first.writes.back() == std::vector<uint8_t>({0x88, 0}));
    assert(f.close_details.back().reason == CloseReason::local_close);
  }
  // A callback may abort another connection; that one is reclaimed by poll().
  {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    f.upgrade(f.second);
    f.abort_other = f.ids[0];
    assert(f.feed(f.second, frame(2, {'x'})) == ERR_OK);
    assert(!f.first.aborted && !f.second.aborted && f.closed.empty());
    f.abort_other = 0;
    f.server.poll();
    assert(f.first.aborted && !f.second.aborted);
    assert(f.closed == std::vector<uint32_t>{f.ids[0]});
    assert(f.feed(f.second, frame(2, {'y'})) == ERR_OK && f.messages.size() == 2);
  }
  for (bool output_failure : {false, true}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    if (output_failure) {
      assert(f.server.enqueueMessage(f.ids.back(), "x", 1) == Result::queued);
      f.first.output_result = ERR_ARG;
      assert(!f.server.flush(f.ids.back()));
    } else {
      f.first.write_result = ERR_ARG;
      assert(f.server.enqueueMessage(f.ids.back(), "x", 1) == Result::error);
    }
    f.server.poll();
    assert(f.first.aborted && f.closed.size() == 1);
  }
  tcp_pcb pcb;
  {
    WebSocketServer server;
    assert(server.startListening(8080));
    Cyw43Guard guard;
    assert(test_listener.accept(test_listener.arg, &pcb, ERR_OK) == ERR_OK);
  }
  assert(pcb.aborted && !pcb.arg && !pcb.recv && !pcb.sent && !pcb.poll && !pcb.error);
}

static void test_http() {
  Fixture f;
  f.start();
  f.accept(f.first);
  f.first.available = 150;
  const std::string request = "GET / HTTP/1.1\r\n\r\n";
  assert(f.feed(f.first, {request.begin(), request.end()}) == ERR_OK);
  // lwIP sends the response when the receive callback returns; the pending
  // flush is confirmed by the next poll.
  assert(!f.first.closed && f.ids.empty() && f.first.outputs == 0);
  f.server.poll();
  assert(f.first.outputs == 1);
  assert(Fixture::sent(f.first, 88) == ERR_OK);
  size_t total = 0;
  for (const auto& bytes : f.first.writes)
    total += bytes.size();
  assert(total > 1200 && !f.first.closed);
  assert(Fixture::sent(f.first, static_cast<u16_t>(total - 88)) == ERR_OK);
  assert(f.first.closed && f.closed.empty());

  f.accept(f.second);
  auto input = std::vector<uint8_t>(handshake, handshake + strlen(handshake));
  append(input, frame(2, {'e', 'a', 'r', 'l', 'y'}));
  assert(f.feed(f.second, input, 11) == ERR_OK);
  assert(f.messages == std::vector<std::vector<uint8_t>>({{'e', 'a', 'r', 'l', 'y'}}));
}

static void test_disconnects() {
  Fixture f;
  f.start();
  f.upgrade(f.first);
  {
    Cyw43Guard guard;
    assert(f.first.recv(f.first.arg, &f.first, nullptr, ERR_OK) == ERR_OK);
  }
  assert(f.first.closed && f.closed.size() == 1 && !f.first.poll);
  assert(f.close_details.back().reason == CloseReason::peer_tcp_closed);
  f.upgrade(f.second);
  {
    Cyw43Guard guard;
    tcp_abort(&f.second);
  }
  assert(f.second.aborted && f.closed.size() == 2);
  assert(f.close_details.back().reason == CloseReason::tcp_error &&
         f.close_details.back().transport_error == ERR_ABRT);
  f.server.poll();
  f.upgrade(f.replacement);
  assert(f.server.abort(f.ids.back()));
  f.server.poll();
  assert(f.replacement.aborted && f.closed.size() == 3);
  assert(f.close_details.back().reason == CloseReason::server_abort);
}

static void test_diagnostics() {
  // Explicit abort must retain its cause on both cleanup paths, even at t=0.
  for (bool timer_cleanup : {false, true}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    uint32_t id = f.ids.back();
    assert(f.server.close(id) && test_now == 0);
    assert(f.server.abort(id));
    if (timer_cleanup)
      assert(Fixture::poll(f.first) == ERR_ABRT);
    else
      f.server.poll();
    assert(f.closed.size() == 1 && f.close_details.size() == 1);
    assert(f.close_details.back().reason == CloseReason::server_abort);
    assert(f.close_details.back().transport_error == 0);
  }
  // A hard error while sending the close frame also precedes the deadline.
  for (bool output_error : {false, true}) {
    Fixture f;
    f.start();
    f.upgrade(f.first);
    uint32_t id = f.ids.back();
    if (output_error)
      f.first.output_result = ERR_ARG;
    else
      f.first.write_result = ERR_ARG;
    assert(f.server.close(id) == output_error);
    assert(f.server.abort(id));
    f.server.poll();
    assert(f.close_details.back().reason ==
           (output_error ? CloseReason::tcp_output_error : CloseReason::tcp_write_error));
    assert(f.close_details.back().transport_error == ERR_ARG);
  }
}

// Message buffers and the frame scratch buffer are allocated once and reused.
static void test_allocations() {
  static unsigned delivered;
  static uint32_t id;
  delivered = 0;
  tcp_pcb pcb;
  WebSocketServer server;
  server.setConnectCallback([](WebSocketServer&, uint32_t connection) { id = connection; });
  server.setMessageCallback([](WebSocketServer&, uint32_t, const void*, size_t) { ++delivered; });
  assert(server.startListening(8080));
  auto feed = [&pcb](std::vector<uint8_t>& bytes) {
    pbuf span{nullptr, bytes.data(), static_cast<u16_t>(bytes.size()),
              static_cast<u16_t>(bytes.size())};
    Cyw43Guard guard;
    assert(pcb.recv(pcb.arg, &pcb, &span, ERR_OK) == ERR_OK);
  };
  {
    Cyw43Guard guard;
    assert(test_listener.accept(test_listener.arg, &pcb, ERR_OK) == ERR_OK);
  }
  std::vector<uint8_t> request(handshake, handshake + strlen(handshake));
  feed(request);
  pcb.record = false;
  auto ack = frame(2, std::vector<uint8_t>(5, 'a'));
  auto input = frame(2, std::vector<uint8_t>(69, 'i'));
  auto fragments = frame(2, std::vector<uint8_t>(30, 'f'), false);
  append(fragments, frame(9, {'p'}));
  append(fragments, frame(0, std::vector<uint8_t>(39, 'f')));
  const uint8_t output[517] = {};
  auto exchange = [&] {
    feed(ack);
    feed(input);
    feed(fragments);
    pcb.available = TCP_SND_BUF;
    assert(server.enqueueMessage(id, output, sizeof(output)) == Result::queued);
    assert(server.sendMessage(id, output, 5));
    assert(server.sendPing(id, output, 4));
    server.poll();
  };
  exchange();
  unsigned long before = allocations;
  unsigned writes = pcb.write_count;
  for (unsigned i = 0; i < 100; ++i)
    exchange();
  assert(allocations == before);
  // Every frame, whatever its size, is exactly one TCP write.
  assert(delivered == 3 * 101 && pcb.write_count == writes + 4 * 100);
}

int main(int argc, char** argv) {
  struct Test { const char* name; void (*run)(); };
  const Test tests[] = {{"send", test_send}, {"flush", test_flush}, {"receive", test_receive},
                        {"limits", test_limits}, {"invalid", test_invalid},
                        {"lifecycle", test_lifecycle}, {"http", test_http},
                        {"disconnects", test_disconnects}, {"diagnostics", test_diagnostics},
                        {"allocations", test_allocations}};
  unsigned ran = 0;
  for (auto& test : tests) {
    if (argc == 1 || std::strcmp(argv[1], test.name) == 0) {
      test.run();
      assert(test_cyw43_depth == 0 && test_listener.closed);
      std::printf("PASS: WebSocket %s\n", test.name);
      ++ran;
    }
  }
  assert(ran);
}
