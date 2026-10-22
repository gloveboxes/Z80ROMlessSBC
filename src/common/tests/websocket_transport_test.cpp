#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cyw43_config.h"
#include "lwip/sys.h"
#include "pico_ws_server/cyw43_guard.h"
#include "pico_ws_server/web_socket_server.h"
#include "web_socket_frame.h"
#include "web_socket_message.h"
#include "lwip/tcp.h"

using Result = WebSocketServer::SendResult;
static constexpr char handshake[] =
    "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";

struct Fixture {
  // PCBs must outlive the server destructor.
  tcp_pcb first, second, replacement;
  WebSocketServer server{2};
  std::vector<uint32_t> ids, closed, delivered_ids;
  std::vector<std::vector<uint8_t>> messages, pongs;
  bool abort_on_message = false, abort_on_connect = false, abort_on_pong = false;
  bool reenter_dispatch = false;

  Fixture() {
    test_now = 0;
    server.setCallbackExtra(this);
    server.setTcpNoDelay(true);
    server.setConnectCallback([](WebSocketServer& s, uint32_t id) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      f.ids.push_back(id);
      if (f.abort_on_connect)
        assert(s.abort(id));
      if (f.reenter_dispatch)
        s.popMessages();
    });
    server.setCloseCallback([](WebSocketServer& s, uint32_t id) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      f.closed.push_back(id);
      // A teardown notification must not resurrect a dying connection.
      assert(s.enqueueMessage(id, "x", 1) == Result::closed);
      s.popMessages();
    });
    server.setMessageCallback([](WebSocketServer& s, uint32_t id, const void* data, size_t len) {
      auto& f = *static_cast<Fixture*>(s.getCallbackExtra());
      auto bytes = static_cast<const uint8_t*>(data);
      assert(bytes[len] == 0);
      f.messages.emplace_back(bytes, bytes + len);
      f.delivered_ids.push_back(id);
      if (f.reenter_dispatch)
        s.popMessages();
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
    f.server.popMessages();
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
      f.server.popMessages();
    else if (recovery == 1)
      assert(Fixture::sent(f.first) == ERR_OK);
    else
      assert(Fixture::poll(f.first) == ERR_OK);
    assert(f.first.writes == std::vector<std::vector<uint8_t>>({{0x8a, 2, 'a', 'b'}}));
    f.first.output_result = ERR_OK;
    f.server.popMessages();
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
  for (size_t i = 0; i < input.size(); ++i)
    assert(f.feed(f.first, {input[i]}) == ERR_OK);
  assert(f.messages.empty());
  f.server.popMessages();
  assert(f.messages.size() == 3 && f.messages[0] == payload &&
         f.messages[1].empty() && f.messages[2] == std::vector<uint8_t>({'h', 'i'}));

  input = frame(1, {'a'}, false);
  append(input, frame(9));
  append(input, frame(10, {'p'}));
  append(input, frame(0, {}, false));
  append(input, frame(0, {'b'}));
  assert(f.feed(f.first, input, 3) == ERR_OK);
  assert(f.pongs.empty() && f.first.writes.back() == std::vector<uint8_t>({0x8a, 0}));
  f.server.popMessages();
  assert(f.messages.back() == std::vector<uint8_t>({'a', 'b'}));
  assert(f.pongs == std::vector<std::vector<uint8_t>>({{'p'}}));

  WebSocketFrame raw;
  raw.reset(1, true, 3);
  const uint8_t bytes[] = {'o', 'w', 'n'}, mask[4] = {};
  raw.append(bytes, 3, mask);
  const uint8_t* storage = raw.getPayload();
  WebSocketMessage message(WebSocketMessage::TEXT, raw.takePayload());
  assert(message.getPayload() == storage && message.getPayload()[3] == 0);
  WebSocketMessage moved(std::move(message));
  assert(moved.getPayload() == storage && moved.getPayloadSize() == 3);
}

static void test_limits() {
  Fixture f;
  assert(!f.server.setReceiveLimits({0, 64, 4, 2}));
  assert(!f.server.setReceiveLimits({65, 64, 4, 2}));
  assert(!f.server.setReceiveLimits({64, 64, 0, 2}));
  assert(!f.server.setReceiveLimits({64, 64, 4, 0}));
  assert(f.server.setReceiveLimits({64, 64, 4, 2}));
  f.start();
  assert(!f.server.setReceiveLimits({64, 64, 4, 2}));
  f.upgrade(f.first);
  f.upgrade(f.second);
  std::vector<uint8_t> burst;
  for (unsigned i = 0; i < 4; ++i)
    append(burst, frame(2, {static_cast<uint8_t>('0' + i)}));
  assert(f.feed(f.first, burst, 5) == ERR_OK);
  assert(f.feed(f.second, burst, 7) == ERR_OK);
  f.reenter_dispatch = true;
  f.server.popMessages();
  assert(f.messages.size() == 4 &&
         std::count(f.delivered_ids.begin(), f.delivered_ids.end(), f.ids[0]) == 2 &&
         std::count(f.delivered_ids.begin(), f.delivered_ids.end(), f.ids[1]) == 2);
  f.server.popMessages();
  assert(f.messages.size() == 8);
  for (unsigned i = 0; i < 4; ++i)
    assert(f.feed(f.first, frame(2)) == ERR_OK);
  assert(f.feed(f.first, frame(2)) == ERR_OK);
  assert(f.first.closed && f.closed.size() == 1);
  assert(f.feed(f.second, frame(2, std::vector<uint8_t>(40, 'x'))) == ERR_OK);
  assert(f.feed(f.second, frame(2, std::vector<uint8_t>(25, 'x'))) == ERR_OK);
  assert(f.second.closed && f.closed.size() == 2);
  f.upgrade(f.replacement);
  assert(f.feed(f.replacement, frame(2, std::vector<uint8_t>(65, 'x'))) == ERR_OK);
  assert(f.replacement.closed && f.closed.size() == 3);
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
    f.server.popMessages();
    assert(f.closed.size() == 1);
  }
}

static void test_lifecycle() {
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
    f.server.popMessages();
    assert(!f.first.aborted);
    test_now += 999;
    assert(Fixture::poll(f.first) == ERR_OK);
    test_now += 1;
    assert(Fixture::poll(f.first) == ERR_ABRT);
    assert(f.first.aborted && f.closed.size() == 1 && !f.first.error && !f.first.poll);
    f.upgrade(f.replacement);
    assert(f.ids.back() != id);
  }
  for (unsigned mode = 0; mode < 3; ++mode) {
    Fixture f;
    f.start();
    f.abort_on_connect = mode == 0;
    f.abort_on_message = mode == 1;
    f.abort_on_pong = mode == 2;
    f.reenter_dispatch = true;
    f.upgrade(f.first);
    if (mode) {
      auto input = frame(mode == 1 ? 2 : 10, {'x'});
      append(input, frame(2, {'n'}));
      assert(f.feed(f.first, input) == ERR_OK);
    }
    f.server.popMessages();
    assert(f.first.aborted && f.closed.size() == 1);
    assert(f.messages.size() == (mode == 1 ? 1 : 0));
    assert(!f.server.abort(f.ids.back()));
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
    f.server.popMessages();
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
  assert(!f.first.closed && f.ids.empty() && f.first.outputs == 1);
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
  f.server.popMessages();
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
  f.upgrade(f.second);
  {
    Cyw43Guard guard;
    tcp_abort(&f.second);
  }
  assert(f.second.aborted && f.closed.size() == 2);
  f.server.popMessages();
  f.upgrade(f.replacement);
  assert(f.server.abort(f.ids.back()));
  f.server.popMessages();
  assert(f.replacement.aborted && f.closed.size() == 3);
}

int main(int argc, char** argv) {
  struct Test { const char* name; void (*run)(); };
  const Test tests[] = {{"send", test_send}, {"flush", test_flush}, {"receive", test_receive},
                        {"limits", test_limits}, {"invalid", test_invalid},
                        {"lifecycle", test_lifecycle}, {"http", test_http},
                        {"disconnects", test_disconnects}};
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
