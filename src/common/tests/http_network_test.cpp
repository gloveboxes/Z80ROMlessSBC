#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../stage10_websocket_terminal/http_network.cpp"
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
static u8_t test_poll_interval;
static bool test_tls;
static altcp_pcb test_pcb;
static altcp_tls_config test_config;
static mbedtls_ssl_context test_ssl;
static ip_addr_t test_ip = {1};
static dns_found_callback test_dns_callback;
static std::vector<void *> test_dns_args;
static std::map<std::string, std::string> test_env = {
    {"HTTP_BASE_URL", "https://raw.githubusercontent.com/AzureSphereCloudEnabledAltair8800/RetroGames/refs/heads/main/"},
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
  assert(test_locks && length == sizeof(HTTP_CA) && memcmp(ca, HTTP_CA, length) == 0);
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
  assert(ssl == &test_ssl && std::string(host) == test_host);
  return test_hostname_result;
}
void altcp_arg(altcp_pcb *p, void *arg) { p->arg = arg; }
void altcp_recv(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, pbuf *, err_t)) { p->recv = fn; }
void altcp_err(altcp_pcb *p, void (*fn)(void *, err_t)) { p->error = fn; }
void altcp_sent(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, u16_t)) { p->sent = fn; }
void altcp_poll(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *), u8_t interval) {
  p->poll = fn;
  test_poll_interval = interval;
}
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
  auto *out = static_cast<uint8_t *>(dst);
  size_t remaining = n;
  for (; p && remaining; p = p->next) {
    size_t length = p->tot_len - (p->next ? p->next->tot_len : 0);
    if (offset >= length) { offset -= length; continue; }
    size_t take = std::min(remaining, length - offset);
    memcpy(out, static_cast<const uint8_t *>(p->payload) + offset, take);
    out += take; remaining -= take; offset = 0;
  }
  assert(!remaining);
  return n;
}
u8_t pbuf_free(pbuf *p) {
  assert(test_locks);
  u8_t n = 0;
  for (; p; p = p->next) { ++test_frees; ++n; }
  return n;
}
void pbuf_cat(pbuf *head, pbuf *tail) {
  assert(test_locks && head && tail);
  for (;;) {
    head->tot_len += tail->tot_len;
    if (!head->next) { head->next = tail; break; }
    head = head->next;
  }
}
pbuf *pbuf_free_header(pbuf *p, u16_t n) {
  assert(test_locks && n <= p->tot_len);
  while (p && n) {
    size_t length = p->tot_len - (p->next ? p->next->tot_len : 0);
    if (n < length) {
      p->payload = static_cast<const uint8_t *>(p->payload) + n;
      p->tot_len -= n;
      return p;
    }
    n -= length;
    pbuf *next = p->next;
    p->next = nullptr;
    pbuf_free(p);
    p = next;
  }
  return p;
}
err_t dns_gethostbyname(const char *host, ip_addr_t *out,
                        dns_found_callback callback, void *arg) {
  assert(test_locks);
  test_host = host;
  *out = test_ip;
  test_dns_callback = callback;
  test_dns_args.push_back(arg);
  return test_defer ? ERR_INPROGRESS : test_dns_result;
}


static void service() { z80_http_network_service(true, 7); }
static uint8_t status();
static void request(const char *name = "LOVE.BAS") {
  z80_http_io_out(21, HTTP_SET_FILENAME);
  while (*name) z80_http_io_out(22, (uint8_t)*name++);
  z80_http_io_out(22, 0);
  z80_http_io_out(21, HTTP_REQUEST_CHUNK);
  test_wire.clear();
  service();
  for (unsigned i = 0; i < 3; ++i) { (void)status(); service(); }
}
static uint8_t status() { return z80_http_io_in(21); }
static std::string output;
static void drain() {
  if (status() != HTTP_DATA_READY) return;
  size_t n = z80_http_io_in(22);
  if (!n) n = 256;
  for (size_t i = 0; i < n; ++i) output += (char)z80_http_io_in(22);
  if (status() != HTTP_EOF) z80_http_io_out(21, HTTP_REQUEST_CHUNK);
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
  for (unsigned i = 0; pending && i < 100; ++i) { service(); drain(); }
  assert(!pending);
}
int main() {
  z80_http_io_init(); service();
  request();
  assert(test_tls && test_port == 443 && test_host == "raw.githubusercontent.com");
  test_write_result = ERR_MEM;
  handshake(); assert(test_wire.empty());
  test_write_result = ERR_OK; test_flush_result = ERR_MEM;
  service();
  assert(test_wire.find("GET /AzureSphereCloudEnabledAltair8800/RetroGames/refs/heads/main/LOVE.BAS HTTP/1.1\r\n") == 0);
  assert(test_wire.find("Host: raw.githubusercontent.com\r\n") != std::string::npos);
  size_t accepted = test_wire.size();
  service(); assert(test_wire.size() == accepted);
  test_flush_result = ERR_OK;
  fragment("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
  service(); drain();
  assert(output == "hello" && status() == HTTP_EOF && !client && !pending);

  /* TLS can deliver headers and then a final body fragment before the normal
   * worker runs. Accept both without asking TLS to retry the last plaintext
   * after close_notify. Withhold credit and finish without any more packets. */
  request(); handshake(); output.clear();
  pbuf empty_buffer = {"", 0};
  assert(receive_buffer(&empty_buffer) == ERR_OK && !pending);
  std::string headers = "HTTP/1.1 200 OK\r\nContent-Length: 1664\r\n\r\n";
  std::string body(1664, 'x');
  pbuf header_buffer = {headers.data(), static_cast<u16_t>(headers.size())};
  pbuf body_buffer = {body.data(), static_cast<u16_t>(body.size())};
  size_t before_credit = test_credited;
  assert(receive_buffer(&header_buffer) == ERR_OK);
  assert(receive_buffer(&body_buffer) == ERR_OK);
  assert(test_credited == before_credit);
  assert(test_pcb.poll && test_poll_interval == 1);
  test_now += 500000;
  cyw43_arch_lwip_begin();
  assert(test_pcb.poll(test_pcb.arg, &test_pcb) == ERR_OK);
  cyw43_arch_lwip_end();
  for (unsigned i = 0; i < 100 && status() != HTTP_EOF; ++i) { service(); drain(); }
  assert(status() == HTTP_EOF && output == body && !pending && !client);
  assert(received_bytes == headers.size() + body.size() && chained_receives == 1);

  /* A complete 16 KiB TLS record split into pool-sized buffers is delivered
   * before worker parsing. It must drain in 512-byte polls without copying or
   * dropping any binary bytes, or depending on further network events. */
  request(); handshake(); output.clear();
  fragment("HTTP/1.1 200 OK\r\nContent-Length: 16384\r\n\r\n");
  std::string record(16384, '\0');
  for (size_t i = 0; i < record.size(); ++i) record[i] = static_cast<char>(i);
  std::vector<pbuf> buffers;
  buffers.reserve(12);
  before_credit = test_credited;
  for (size_t i = 0; i < record.size(); i += 1460) {
    buffers.push_back({record.data() + i,
                       static_cast<u16_t>(std::min(size_t(1460), record.size() - i))});
    assert(receive_buffer(&buffers.back()) == ERR_OK);
  }
  assert(test_credited == before_credit && pending->tot_len == record.size());
  for (unsigned i = 0; i < 150 && status() != HTTP_EOF; ++i) { service(); drain(); }
  assert(status() == HTTP_EOF && output == record && !pending && !client);

  /* Continue returning TCP credit while new buffers arrive. Consumed pbuf
   * prefixes must be freed, so the receive chain never grows with file size. */
  request(); handshake(); output.clear();
  fragment("HTTP/1.1 200 OK\r\nContent-Length: 70000\r\n\r\n");
  std::string large(70000, '\0');
  for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>(i);
  std::vector<pbuf> continuous;
  continuous.reserve(50);
  before_credit = test_credited;
  for (size_t offset = 0; offset < large.size(); offset += 1460) {
    u16_t n = static_cast<u16_t>(std::min(size_t(1460), large.size() - offset));
    while (pending && pending->tot_len + n > TCP_WND) { service(); drain(); }
    continuous.push_back({large.data() + offset, n});
    assert(receive_buffer(&continuous.back()) == ERR_OK);
    service(); drain();
    assert(!pending || pending->tot_len <= TCP_WND);
    assert(test_credited - before_credit <= offset + n);
  }
  for (unsigned i = 0; i < 150 && status() != HTTP_EOF; ++i) { service(); drain(); }
  assert(status() == HTTP_EOF && output == large && !pending && !client);

  request(); handshake();
  std::string excessive(RECEIVE_MAX, 'x');
  pbuf full = {excessive.data(), static_cast<u16_t>(excessive.size())};
  pbuf extra = {"x", 1};
  assert(receive_buffer(&full) == ERR_OK);
  assert(receive_buffer(&extra) == ERR_OK); // explicit failure owns/frees input
  service();
  assert(status() == HTTP_ERROR && !pending && !client);
  assert(strstr(failure_detail, "backlog exceeds TCP/TLS bound"));

  /* Backpressure retains receive buffers until FT consumes bytes. New buffers
   * are chained within the TCP/TLS bound, with credit returned only on use. */
  request("http://192.168.1.10:8000/BIG.BIN"); handshake(); output.clear();
  assert(!test_tls && test_port == 8000);
  fragment("HTTP/1.1 200 OK\r\nContent-Length: 4000\r\n\r\n");
  std::string data(4000, 'x');
  pbuf p = {data.data(), 3000};
  assert(receive_buffer(&p) == ERR_OK);
  for (unsigned i = 0; i < 20; ++i) service();
  assert(pending && pending->tot_len < 3000);
  size_t held = pending->tot_len;
  before_credit = test_credited;
  pbuf next = {data.data() + 3000, 1000};
  assert(receive_buffer(&next) == ERR_OK);
  assert(pending->tot_len == held + 1000 && test_credited == before_credit);
  test_now += 60000000; // FT can pause without expiring retained TCP data
  service(); assert(pending && status() != HTTP_ERROR);
  for (unsigned i = 0; pending && i < 100; ++i) { test_now += 1000000; service(); drain(); }
  for (unsigned i = 0; i < 100 && status() != HTTP_EOF; ++i) { service(); drain(); }
  assert(status() == HTTP_EOF && output == data && !pending && !client);

  request("http://localhost/test"); handshake();
  test_now += 30001000; service();
  assert(status() == HTTP_ERROR && !client);
  assert(strstr(failure_detail, "stage=awaiting response"));
  test_defer = true;
  request("http://lookup.example/file");
  test_now += 30001000; service();
  assert(status() == HTTP_ERROR && !client && strstr(failure_detail, "stage=DNS"));
  test_defer = false;
  request(); // TCP/TLS completion callback has not arrived.
  test_now += 30001000; service();
  assert(status() == HTTP_ERROR && !client && strstr(failure_detail, "stage=TCP/TLS"));
  request(); handshake();
  fragment("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab");
  test_now += 30001000; service();
  assert(status() == HTTP_ERROR && !client && strstr(failure_detail, "stage=receiving"));
  test_time = false; request();
  assert(status() == HTTP_ERROR && !client); test_time = true;
  test_hostname_result = -1; request();
  assert(status() == HTTP_ERROR && !client); test_hostname_result = 0;
  test_configure = false; tls_config = nullptr; request();
  assert(status() == HTTP_ERROR && !client); test_configure = true;

  /* Old DNS answers cannot attach to a closed/new transfer. */
  test_defer = true; request("http://old.example/file");
  void *old = test_dns_args.back();
  request("http://new.example/file");
  unsigned allocations = test_aborts;
  cyw43_arch_lwip_begin(); test_dns_callback("old.example", &test_ip, old); cyw43_arch_lwip_end();
  assert(!client && test_aborts == allocations);
  cyw43_arch_lwip_begin(); test_dns_callback("new.example", &test_ip, test_dns_args.back()); cyw43_arch_lwip_end();
  handshake();
  z80_http_network_service(false, 8);
  assert(status() == HTTP_ERROR && !client);
  assert(test_locks == 0 && test_credited > 4000 && test_frees > 0);
  puts("PASS: async HTTP/TLS, buffered TLS retry poll, GET construction, backpressure, staged timeouts, trust, stale DNS and disconnect");
}
