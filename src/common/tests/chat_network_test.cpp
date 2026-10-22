#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../stage10_websocket_terminal/chat_network.cpp"
extern "C" {
#include "../../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"
}

static unsigned test_locks, test_writes, test_flushes, test_aborts, test_frees;
static size_t test_credited;
static bool test_time = true, test_allocate = true, test_configure = true, test_defer;
static uint8_t test_env_error;
static uint64_t test_now;
static err_t test_write_result = ERR_OK, test_flush_result = ERR_OK;
static err_t test_dns_result = ERR_OK, test_connect_result = ERR_OK;
static int test_hostname_result;
static std::string test_wire, test_host;
static uint16_t test_port;
static bool test_tls;
static altcp_pcb test_pcb;
static altcp_tls_config test_config;
static mbedtls_ssl_context test_ssl;
static ip_addr_t test_ip = {1};
static dns_found_callback test_dns_callback;
static std::vector<void *> test_dns_args;
static std::map<std::string, std::string> test_env = {
    {"CHAT_PROVIDER", "ollama"},
    {"CHAT_ENDPOINT", "http://192.168.1.26:11434/v1/chat/completions"},
    {"CHAT_OPENAI_KEY", "dummy-secret-never-send-to-ollama"},
};

void cyw43_arch_lwip_begin() { assert(test_locks++ == 0); }
void cyw43_arch_lwip_end() { assert(test_locks-- == 1); }
uint64_t time_us_64() { return test_now; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return test_now + ms * 1000ull; }
bool time_reached(absolute_time_t t) { return test_now >= t; }
time_t z80_network_time(time_t *) { return test_time ? 1792627200 : 0; }
void mbedtls_platform_zeroize(void *p, size_t n) { memset(p, 0, n); }
void environment_io_init(const char *) {}
void environment_io_close() {}
uint8_t environment_input(uint8_t port) {
  assert(port == ENVIRONMENT_PORT_COMMAND);
  return test_env_error;
}
bool environment_io_get(const char *key, char *out, size_t capacity) {
  auto found = test_env.find(key);
  if (found == test_env.end()) { out[0] = '\0'; return false; }
  assert(found->second.size() < capacity);
  strcpy(out, found->second.c_str());
  return true;
}
altcp_tls_config *altcp_tls_create_config_client(const u8_t *ca, size_t length) {
  assert(test_locks && length == sizeof(CHAT_CA) && memcmp(ca, CHAT_CA, length) == 0);
  return test_configure ? &test_config : nullptr;
}
altcp_pcb *altcp_tls_new(altcp_tls_config *config, u8_t type) {
  assert(test_locks && config == &test_config && type == IPADDR_TYPE_V4);
  test_tls = true;
  test_pcb = {};
  return test_allocate ? &test_pcb : nullptr;
}
altcp_pcb *altcp_tcp_new_ip_type(u8_t type) {
  assert(test_locks && type == IPADDR_TYPE_V4);
  test_tls = false;
  test_pcb = {};
  return test_allocate ? &test_pcb : nullptr;
}
void *altcp_tls_context(altcp_pcb *pcb) { assert(pcb == &test_pcb); return &test_ssl; }
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *ssl, const char *host) {
  assert(ssl == &test_ssl && std::string(host) == "api.openai.com");
  return test_hostname_result;
}
void altcp_arg(altcp_pcb *p, void *arg) { p->arg = arg; }
void altcp_recv(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, pbuf *, err_t)) { p->recv = fn; }
void altcp_err(altcp_pcb *p, void (*fn)(void *, err_t)) { p->error = fn; }
void altcp_sent(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, u16_t)) { p->sent = fn; }
void altcp_poll(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *), u8_t) { p->poll = fn; }
void altcp_abort(altcp_pcb *p) {
  assert(test_locks && p == &test_pcb && !p->arg && !p->recv && !p->error &&
         !p->sent && !p->poll);
  ++test_aborts;
}
err_t altcp_connect(altcp_pcb *p, const ip_addr_t *, u16_t port,
                    err_t (*fn)(void *, altcp_pcb *, err_t)) {
  assert(test_locks && p == &test_pcb);
  test_port = port;
  p->connected = fn;
  return test_connect_result;
}
err_t altcp_write(altcp_pcb *, const void *data, u16_t n, u8_t flags) {
  assert(test_locks && n <= WORK_BYTES && flags == TCP_WRITE_FLAG_COPY);
  ++test_writes;
  if (test_write_result == ERR_OK)
    test_wire.append(static_cast<const char *>(data), n);
  return test_write_result;
}
err_t altcp_output(altcp_pcb *) { assert(test_locks); ++test_flushes; return test_flush_result; }
void altcp_recved(altcp_pcb *, u16_t n) { assert(test_locks); test_credited += n; }
u16_t pbuf_copy_partial(const pbuf *p, void *dst, u16_t n, u16_t offset) {
  assert(offset + n <= p->tot_len);
  memcpy(dst, static_cast<const char *>(p->payload) + offset, n);
  return n;
}
u8_t pbuf_free(pbuf *) { assert(test_locks); ++test_frees; return 1; }
err_t dns_gethostbyname(const char *host, ip_addr_t *out,
                        dns_found_callback callback, void *arg) {
  assert(test_locks);
  test_host = host;
  *out = test_ip;
  test_dns_callback = callback;
  test_dns_args.push_back(arg);
  return test_defer ? ERR_INPROGRESS : test_dns_result;
}

static io_service_result_t test_reply;
static void operation(uint8_t op, const std::string &data = {}) {
  std::vector<uint8_t> request(1, op);
  request.insert(request.end(), data.begin(), data.end());
  test_reply = {};
  assert(z80_chat_service(request.data(), request.size(), &test_reply) == IO_SERVICE_OK);
}
static void service() { z80_chat_network_service(true, 7); }
static std::string drain() {
  std::string text;
  for (;;) {
    operation(5, std::string("\0\1", 2));
    size_t n = test_reply.data[1] | (static_cast<size_t>(test_reply.data[2]) << 8);
    assert(test_reply.length == n + 3 && n <= 256);
    text.append(reinterpret_cast<char *>(test_reply.data + 3), n);
    if (!n) break;
  }
  return text;
}
static void request(const std::string &json = "{\"stream\":true}") {
  operation(2);
  operation(0);
  operation(1, json);
  test_wire.clear();
  operation(3);
}
static void handshake() {
  cyw43_arch_lwip_begin();
  assert(test_pcb.connected(test_pcb.arg, &test_pcb, ERR_OK) == ERR_OK);
  cyw43_arch_lwip_end();
  service();
}
static err_t receive_buffer(pbuf *p) {
  cyw43_arch_lwip_begin();
  err_t result = test_pcb.recv(test_pcb.arg, &test_pcb, p, ERR_OK);
  cyw43_arch_lwip_end();
  return result;
}
static void fragment(const std::string &text) {
  pbuf p = {text.data(), static_cast<u16_t>(text.size())};
  assert(receive_buffer(&p) == ERR_OK);
  for (unsigned i = 0; pending && i < 100; ++i) service();
  assert(!pending);
}
static void expect_error(const char *text) {
  service();
  assert(!z80_chat_active() && !client && !pending);
  assert(drain().find(text) != std::string::npos);
}
static const std::string test_headers =
    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n";
static std::string event(const std::string &text) {
  return "data: {\"choices\":[{\"delta\":{\"content\":\"" + text + "\"}}]}\n\n";
}

int main() {
  service();
  test_time = false;
  request(std::string(8191, 'x'));
  assert(test_host == "192.168.1.26" && test_port == 11434 && !test_tls);
  test_write_result = ERR_MEM;
  handshake();
  assert(test_wire.empty() && test_writes == 1);
  test_write_result = ERR_OK;
  test_flush_result = ERR_MEM;
  for (unsigned i = 0; i < 30; ++i) {
    size_t previous = test_wire.size();
    unsigned writes_before = test_writes;
    service();
    assert(test_wire.size() - previous <= WORK_BYTES && test_writes - writes_before <= 1);
  }
  size_t separator = test_wire.find("\r\n\r\n");
  assert(separator != std::string::npos);
  assert(test_wire.substr(separator + 4) == std::string(8191, 'x'));
  assert(test_wire.find("Content-Length: 8191\r\n") != std::string::npos);
  assert(test_wire.find("Authorization") == std::string::npos);
  size_t accepted = test_wire.size();
  service();
  assert(test_wire.size() == accepted && test_flushes > 30);
  test_flush_result = ERR_OK;
  fragment(test_headers.substr(0, 20));
  fragment(test_headers.substr(20) + event("Hello\\n"));
  assert(drain() == "Hello\n");
  fragment("data: [DONE]\n\n");
  assert(!z80_chat_active() && !client && test_locks == 0);

  /* Stop consuming CP/M output: retain a pbuf, then reject the next chain.
   * Resume consumption and retry that exact chain; no duplicate/lost bytes. */
  request();
  handshake();
  fragment(test_headers);
  for (unsigned i = 0; i < 4; ++i) fragment(event(std::string(1000, 'a')));
  std::string fifth = event(std::string(1000, 'b'));
  pbuf p1 = {fifth.data(), static_cast<u16_t>(fifth.size())};
  assert(receive_buffer(&p1) == ERR_OK);
  for (unsigned i = 0; i < 5; ++i) service();
  std::string sixth = event("last");
  pbuf p2 = {sixth.data(), static_cast<u16_t>(sixth.size())};
  unsigned before_free = test_frees;
  if (pending) {
    assert(receive_buffer(&p2) == ERR_MEM);
    assert(test_frees == before_free);
  } else {
    /* The decoded token can be pending even after its entire pbuf was read. */
    assert(receive_buffer(&p2) == ERR_OK);
    service();
    assert(pending);
  }
  size_t credit = test_credited;
  for (unsigned i = 0; i < 5; ++i) service();
  assert(test_credited == credit);
  std::string text = drain();
  for (unsigned i = 0; i < 10; ++i) { service(); text += drain(); }
  if (text.find("last") == std::string::npos) fragment(sixth);
  text += drain();
  assert(text == std::string(4000, 'a') + std::string(1000, 'b') + "last");
  fragment("data: [DONE]\n\n");

  request();
  handshake();
  fragment(test_headers);
  fragment(event(std::string(2048, 'x')));
  fragment(event(std::string(2048, 'y')));
  test_now += TIMEOUT_MS * 1000ull;
  service();
  text = drain();
  assert(text.find(std::string(2048, 'x') + std::string(2048, 'y')) == 0);
  assert(text.substr(4096) == "\nChat: HTTP stream timed out (120 seconds)\n");
  assert(!z80_chat_active() && !client);

  request();
  handshake();
  fragment(test_headers + event("partial"));
  receive_buffer(nullptr);
  expect_error("closed before [DONE]");
  request();
  handshake();
  fragment("HTTP/1.1 401 Unauthorized\r\n\r\n");
  assert(drain().find("401/403") != std::string::npos && !client);

  test_defer = true;
  request();
  void *stale = test_dns_args.back();
  operation(2);
  request();
  cyw43_arch_lwip_begin();
  test_dns_callback(test_host.c_str(), &test_ip, stale);
  assert(!client);
  test_dns_callback(test_host.c_str(), &test_ip, test_dns_args.back());
  assert(client);
  cyw43_arch_lwip_end();
  test_now += TIMEOUT_MS * 1000ull;
  expect_error("timed out");
  stale = test_dns_args.back();
  cyw43_arch_lwip_begin();
  test_dns_callback(test_host.c_str(), &test_ip, stale);
  cyw43_arch_lwip_end();
  assert(!client);
  test_defer = false;

  test_dns_result = ERR_ABRT;
  request();
  expect_error("DNS lookup start failed");
  test_dns_result = ERR_OK;
  test_connect_result = ERR_ABRT;
  request();
  expect_error("connection start failed");
  test_connect_result = ERR_OK;
  request();
  cyw43_arch_lwip_begin();
  test_pcb.connected(test_pcb.arg, &test_pcb, ERR_ABRT);
  cyw43_arch_lwip_end();
  expect_error("TCP/TLS connection failed");
  request();
  test_write_result = ERR_ABRT;
  handshake();
  expect_error("request write failed");
  test_write_result = ERR_OK;
  request();
  test_flush_result = ERR_ABRT;
  handshake();
  expect_error("request flush failed");
  test_flush_result = ERR_OK;
  request();
  handshake();
  fragment(std::string(RECEIVE_MAX + 1, 'x'));
  expect_error("exceeds 16384");
  request();
  handshake();
  fragment(test_headers);
  size_t credited_before = test_credited;
  std::string padding(RECEIVE_MAX, '\n');
  pbuf padding_pbuf = {padding.data(), static_cast<u16_t>(padding.size())};
  assert(receive_buffer(&padding_pbuf) == ERR_OK && test_credited == credited_before);
  service();
  assert(test_credited - credited_before == WORK_BYTES && pending);
  for (unsigned i = 1; i < RECEIVE_MAX / WORK_BYTES; ++i) service();
  assert(!pending);
  assert(test_credited - credited_before == RECEIVE_MAX && z80_chat_active());
  fragment("data: [DONE]\n\n");
  assert(!z80_chat_active() && !client);
  request();
  handshake();
  std::string final_response = test_headers + event("before FIN") + "data: [DONE]\n\n";
  pbuf final_pbuf = {final_response.data(), static_cast<u16_t>(final_response.size())};
  receive_buffer(&final_pbuf);
  receive_buffer(nullptr);
  service();
  assert(!z80_chat_active() && drain() == "before FIN" && !client);

  request();
  handshake();
  z80_chat_network_service(false, 8);
  assert(!z80_chat_active() && !client && drain().find("disconnected") != std::string::npos);
  service();
  test_allocate = false;
  request();
  expect_error("allocation failed");
  test_allocate = true;
  request();
  handshake();
  auto old_error = test_pcb.error;
  void *old_arg = test_pcb.arg;
  cyw43_arch_lwip_begin();
  old_error(old_arg, ERR_ABRT);  // lwIP has already freed the PCB.
  cyw43_arch_lwip_end();
  unsigned aborts_before = test_aborts;
  expect_error("connection failed");
  assert(test_aborts == aborts_before);
  request();
  cyw43_arch_lwip_begin();
  old_error(old_arg, ERR_ABRT);
  cyw43_arch_lwip_end();
  assert(client);
  operation(2);

  test_env["CHAT_PROVIDER"] = "openai";
  request();
  expect_error("synchronized SNTP");
  test_time = true;
  test_configure = false;
  request();
  expect_error("trust configuration");
  test_configure = true;
  test_hostname_result = -1;
  request();
  expect_error("hostname verification");
  test_hostname_result = 0;
  request();
  assert(test_tls && test_host == "api.openai.com" && test_port == 443);
  handshake();
  service();
  assert(test_wire.find("Authorization: Bearer dummy-secret-never-send-to-ollama\r\n") != std::string::npos);
  assert(test_wire.find("POST /v1/chat/completions HTTP/1.1\r\n") == 0);
  operation(2);
  assert(!client && header[0] == 0);
  test_env["CHAT_OPENAI_KEY"] = "bad\r\ninjected";
  request();
  expect_error("Invalid CHAT_OPENAI_KEY");
  test_env["CHAT_OPENAI_KEY"] = "";
  request();
  expect_error("Set CHAT_OPENAI_KEY");
  test_env["CHAT_PROVIDER"] = "other";
  request();
  expect_error("CHAT_PROVIDER must be");
  test_env["CHAT_PROVIDER"] = "compatible";
  for (const char *url : {"http://user@host/v1", "ftp://host/v1", "http://host:65536/v1",
                          "http://host:0/v1", "http://host:/v1", "http://host/v1\r\nx",
                          "http://host", "http://host/v1#fragment"}) {
    test_env["CHAT_ENDPOINT"] = url;
    request();
    expect_error("Invalid CHAT_ENDPOINT");
  }
  test_env_error = 254;
  request();
  expect_error("Cannot read CHAT ENV");
  test_env_error = 0;
  test_env["CHAT_ENDPOINT"] = "http://192.168.1.26:11434/v1/chat/completions";
  request();
  handshake();
  fragment(test_headers + event(std::string(1000, 'x')));
  operation(0);
  assert(!z80_chat_active() && !client);
  operation(2);
  assert(drain().empty());
  operation(1, "{}");
  operation(3);
  assert(z80_chat_active());
  operation(2);
  assert(test_locks == 0);
  puts("PASS: actual CHAT worker/HTTP client, bounded writes, TCP backpressure, cancellation and TLS lifecycle");
}
