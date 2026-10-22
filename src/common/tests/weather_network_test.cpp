#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "../../stage10_websocket_terminal/weather_network.cpp"

static unsigned lock_depth, writes, flushes, aborts, freed, results;
static bool wifi = true, synchronized = true, configured = true;
static bool allocate = true, defer_dns;
static bool (*engine_start)(const char *);
static void (*engine_cancel)(void);
static bool engine_online;
static uint64_t now;
static err_t write_result = ERR_OK;
static err_t flush_result = ERR_OK;
static err_t close_result = ERR_OK;
static std::string request_bytes, result_body, result_error;
static unsigned result_status;
static altcp_pcb pcb;
static altcp_tls_config tls;
static mbedtls_ssl_context ssl;
static ip_addr_t ip = {1};
static dns_found_callback dns_callback;
static std::vector<void *> dns_args;

void cyw43_arch_lwip_begin() { ++lock_depth; }
void cyw43_arch_lwip_end() { assert(lock_depth); --lock_depth; }
uint64_t time_us_64(void) { return now; }
absolute_time_t make_timeout_time_ms(uint32_t delay) { return now + delay * 1000ull; }
bool time_reached(absolute_time_t time) { return now >= time; }
time_t z80_network_time(time_t *) { return synchronized ? 1704067200 : 0; }
size_t z80_io_ports_get_network_address(char *address, size_t capacity) {
  assert(capacity >= 10);
  strcpy(address, wifi ? "192.0.2.1" : "");
  return strlen(address);
}
void z80_weather_init(bool (*start_fn)(const char *), void (*cancel_fn)(void)) {
  engine_start = start_fn;
  engine_cancel = cancel_fn;
}
void z80_weather_poll(bool online) { engine_online = online; }
void z80_weather_result(unsigned status, const char *body, size_t length,
                        const char *error) {
  ++results;
  result_status = status;
  result_body.assign(body ? body : "", length);
  result_error = error ? error : "";
}
altcp_tls_config *altcp_tls_create_config_client(const u8_t *ca, size_t length) {
  assert(lock_depth && length == sizeof(WEATHER_CA));
  assert(memcmp(ca, WEATHER_CA, length) == 0);
  return configured ? &tls : nullptr;
}
altcp_pcb *altcp_tls_new(altcp_tls_config *config, u8_t type) {
  assert(lock_depth && config == &tls && type == IPADDR_TYPE_V4);
  pcb = {};
  return allocate ? &pcb : nullptr;
}
void *altcp_tls_context(altcp_pcb *connection) {
  assert(connection == &pcb);
  return &ssl;
}
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *context, const char *host) {
  assert(context == &ssl && std::string(host) == "api.openweathermap.org");
  return 0;
}
void altcp_arg(altcp_pcb *p, void *arg) { p->arg = arg; }
void altcp_recv(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, pbuf *, err_t)) { p->recv = fn; }
void altcp_err(altcp_pcb *p, void (*fn)(void *, err_t)) { p->error = fn; }
void altcp_sent(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *, u16_t)) { p->sent = fn; }
void altcp_poll(altcp_pcb *p, err_t (*fn)(void *, altcp_pcb *), u8_t) { p->poll = fn; }
err_t altcp_close(altcp_pcb *p) {
  assert(lock_depth && p->arg == nullptr && !p->error && !p->recv);
  return close_result;
}
void altcp_abort(altcp_pcb *p) {
  assert(lock_depth && p == &pcb && !p->error && !p->recv);
  ++aborts;
}
err_t altcp_connect(altcp_pcb *p, const ip_addr_t *, u16_t port,
                    err_t (*fn)(void *, altcp_pcb *, err_t)) {
  assert(lock_depth && port == 443);
  p->connected = fn;
  return ERR_OK;
}
err_t altcp_write(altcp_pcb *, const void *data, u16_t length, u8_t flags) {
  assert(lock_depth && flags == TCP_WRITE_FLAG_COPY);
  ++writes;
  if (write_result == ERR_OK)
    request_bytes.assign(static_cast<const char *>(data), length);
  return write_result;
}
err_t altcp_output(altcp_pcb *) { assert(lock_depth); ++flushes; return flush_result; }
void altcp_recved(altcp_pcb *, u16_t) { assert(lock_depth); }
u16_t pbuf_copy_partial(const pbuf *p, void *dst, u16_t length, u16_t offset) {
  assert(offset == 0 && length <= p->tot_len);
  memcpy(dst, p->payload, length);
  return length;
}
u8_t pbuf_free(pbuf *) { ++freed; return 1; }
err_t dns_gethostbyname(const char *host, ip_addr_t *out,
                        dns_found_callback callback, void *arg) {
  assert(lock_depth && std::string(host) == "api.openweathermap.org");
  dns_callback = callback;
  dns_args.push_back(arg);
  *out = ip;
  return defer_dns ? ERR_INPROGRESS : ERR_OK;
}

static void handshake(void) {
  cyw43_arch_lwip_begin();
  assert(pcb.connected(pcb.arg, &pcb, ERR_OK) == ERR_OK);
  cyw43_arch_lwip_end();
}
static err_t fragment(const std::string &text) {
  pbuf buffer = {text.data(), (u16_t)text.size()};
  cyw43_arch_lwip_begin();
  err_t result = pcb.recv(pcb.arg, &pcb, &buffer, ERR_OK);
  cyw43_arch_lwip_end();
  return result;
}
static void eof(void) {
  cyw43_arch_lwip_begin();
  pcb.recv(pcb.arg, &pcb, nullptr, ERR_OK);
  cyw43_arch_lwip_end();
  z80_weather_network_poll();
}

int main(void) {
  synchronized = false;
  z80_weather_network_poll();
  assert(!engine_online && results == 0);
  synchronized = true;
  z80_weather_network_poll();
  assert(engine_online);
  configured = false;
  assert(engine_start("/test?appid=dummy"));
  z80_weather_network_poll();
  assert(results == 1 && result_error == "Weather TLS configuration failed");
  configured = true;
  assert(engine_start("/test?appid=dummy"));
  cyw43_arch_lwip_begin();
  assert(pcb.poll(pcb.arg, &pcb) == ERR_OK && writes == 0);
  cyw43_arch_lwip_end();
  write_result = ERR_MEM;
  handshake();
  assert(writes == 1 && request_bytes.empty());
  write_result = ERR_OK;
  flush_result = ERR_MEM;
  cyw43_arch_lwip_begin();
  assert(pcb.poll(pcb.arg, &pcb) == ERR_OK);
  assert(pcb.poll(pcb.arg, &pcb) == ERR_OK);
  cyw43_arch_lwip_end();
  assert(writes == 2 && flushes == 2);
  assert(request_bytes.find("GET /test?appid=dummy HTTP/1.1\r\n") == 0);
  assert(request_bytes.find("\r\nHost: api.openweathermap.org\r\n") != std::string::npos);
  assert(fragment("HTTP/1.1 200 OK\r\nContent-Len") == ERR_OK);
  assert(fragment("gth: 2\r\n\r\n{}") == ERR_OK);
  eof();
  assert(results == 2 && result_status == 200 && result_body == "{}" && result_error.empty());
  assert(freed == 2);

  defer_dns = true;
  assert(engine_start("/test"));
  void *stale = dns_args.back();
  engine_cancel();
  assert(engine_start("/test"));
  cyw43_arch_lwip_begin();
  dns_callback(HOST, &ip, stale);
  assert(client == nullptr);
  dns_callback(HOST, &ip, dns_args.back());
  assert(client != nullptr);
  cyw43_arch_lwip_end();
  now += TIMEOUT_MS * 1000ull;
  z80_weather_network_poll();
  assert(result_error == "Weather HTTPS request timed out" && aborts == 1);

  assert(engine_start("/test"));
  stale = dns_args.back();
  now += TIMEOUT_MS * 1000ull;
  z80_weather_network_poll();
  cyw43_arch_lwip_begin();
  dns_callback(HOST, &ip, stale);
  cyw43_arch_lwip_end();
  assert(client == nullptr && result_error == "Weather HTTPS request timed out");
  defer_dns = false;
  allocate = false;
  assert(engine_start("/test"));
  z80_weather_network_poll();
  assert(result_error == "Weather TLS allocation failed");
  allocate = true;
  assert(engine_start("/test"));
  handshake();
  assert(fragment(std::string(16385, 'X')) == ERR_ABRT);
  z80_weather_network_poll();
  assert(result_error == "Weather response too large or receive failed");

  assert(engine_start("/test"));
  handshake();
  wifi = false;
  z80_weather_network_poll();
  assert(!engine_online && result_error == "Weather network/time unavailable");
  wifi = true;
  assert(engine_start("/test"));
  handshake();
  fragment("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}");
  close_result = ERR_MEM;
  eof();
  assert(result_status == 401 && result_error.empty());
  assert(client == nullptr && lock_depth == 0);
  puts("PASS: weather async DNS/TLS, single GET, bounds, timeout and cancellation");
}
