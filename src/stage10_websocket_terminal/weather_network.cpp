#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform_util.h"
#include "weather_ca.h"

extern "C" {
#include "../common/weather.h"
#include "../common/weather_http.h"
#include "z80sbc/network_time.h"
}

namespace {
constexpr char HOST[] = "api.openweathermap.org";
constexpr uint32_t TIMEOUT_MS = 15000;
altcp_tls_config *tls_config;
altcp_pcb *client;
bool initialized, active, done, request_sent, handshake_ready;
uint32_t generation;
absolute_time_t deadline;
const char *failure;
char request[768];
char wire[16385];
size_t received;

bool release_client(bool abort) {
  altcp_pcb *pcb = client;
  client = nullptr;
  if (!pcb)
    return false;
  altcp_arg(pcb, nullptr);
  altcp_recv(pcb, nullptr);
  altcp_err(pcb, nullptr);
  altcp_sent(pcb, nullptr);
  altcp_poll(pcb, nullptr, 0);
  if (abort || altcp_close(pcb) != ERR_OK) {
    altcp_abort(pcb);
    return true;
  }
  return false;
}

void complete(const char *error) {
  failure = error;
  active = false;
  done = true;
}

void socket_error(void *, err_t) {
  client = nullptr;
  complete("Weather DNS/TCP/TLS connection failed");
}

err_t send_request() {
  if (!handshake_ready)
    return ERR_OK;
  if (!request_sent) {
    err_t result = altcp_write(client, request, (u16_t)strlen(request),
                               TCP_WRITE_FLAG_COPY);
    if (result == ERR_MEM)
      return ERR_OK;
    if (result != ERR_OK) {
      complete("Weather request write failed");
      release_client(true);
      return ERR_ABRT;
    }
    request_sent = true;
  }
  // A failed flush may follow an accepted write; never enqueue the GET twice.
  altcp_output(client);
  return ERR_OK;
}

err_t connected(void *, altcp_pcb *, err_t error) {
  if (error != ERR_OK) {
    complete("Weather TLS connection failed");
    release_client(true);
    return ERR_ABRT;
  }
  handshake_ready = true;
  return send_request();
}

err_t sent(void *, altcp_pcb *, u16_t) { return send_request(); }
err_t poll(void *, altcp_pcb *) { return send_request(); }

err_t receive(void *, altcp_pcb *pcb, pbuf *data, err_t error) {
  if (!data) {
    complete(error == ERR_OK ? nullptr : "Weather response receive failed");
    return release_client(false) ? ERR_ABRT : ERR_OK;
  }
  size_t length = data->tot_len;
  bool accepted = error == ERR_OK && length <= sizeof(wire) - 1 - received &&
                  pbuf_copy_partial(data, wire + received, (u16_t)length, 0) == length;
  altcp_recved(pcb, data->tot_len);
  pbuf_free(data);
  if (!accepted) {
    complete("Weather response too large or receive failed");
    release_client(true);
    return ERR_ABRT;
  }
  received += length;
  wire[received] = '\0';
  return ERR_OK;
}

void resolved(const char *, const ip_addr_t *address, void *arg) {
  if (!active || (uintptr_t)arg != generation)
    return;
  if (!address) {
    complete("Weather DNS lookup failed");
    return;
  }
  client = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
  if (!client) {
    complete("Weather TLS allocation failed");
    return;
  }
  auto *tls = static_cast<mbedtls_ssl_context *>(altcp_tls_context(client));
  if (!tls || mbedtls_ssl_set_hostname(tls, HOST) != 0) {
    complete("Weather TLS hostname setup failed");
    release_client(true);
    return;
  }
  altcp_recv(client, receive);
  altcp_err(client, socket_error);
  altcp_sent(client, sent);
  altcp_poll(client, poll, 1);
  if (altcp_connect(client, address, 443, connected) != ERR_OK) {
    complete("Weather connection start failed");
    release_client(true);
  }
}

void cancel() {
  cyw43_arch_lwip_begin();
  ++generation;
  active = done = false;
  release_client(true);
  cyw43_arch_lwip_end();
}

bool start(const char *path) {
  int length = snprintf(request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\n"
      "Connection: close\r\n\r\n", path, HOST);
  if (length <= 0 || (size_t)length >= sizeof(request))
    return false;
  cyw43_arch_lwip_begin();
  if (!tls_config)
    tls_config = altcp_tls_create_config_client(
        reinterpret_cast<const u8_t *>(WEATHER_CA), sizeof(WEATHER_CA));
  if (!tls_config) {
    complete("Weather TLS configuration failed");
    cyw43_arch_lwip_end();
    return true;
  }
  active = true;
  done = request_sent = handshake_ready = false;
  received = 0;
  wire[0] = '\0';
  failure = nullptr;
  if (++generation == 0)
    ++generation;
  deadline = make_timeout_time_ms(TIMEOUT_MS);
  ip_addr_t address;
  err_t result = dns_gethostbyname(HOST, &address, resolved,
                                  reinterpret_cast<void *>((uintptr_t)generation));
  if (result == ERR_OK)
    resolved(HOST, &address, reinterpret_cast<void *>((uintptr_t)generation));
  else if (result != ERR_INPROGRESS)
    complete("Weather DNS lookup start failed");
  cyw43_arch_lwip_end();
  return true;
}
}  // namespace

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)(time_us_64() / 1000);
}

extern "C" void z80_weather_network_poll(void) {
  if (!initialized) {
    z80_weather_init(start, cancel);
    initialized = true;
  }
  char address[16];
  bool online = z80_io_ports_get_network_address(address, sizeof(address)) != 0 &&
                z80_network_time(nullptr) != 0;
  if (active && (!online || time_reached(deadline))) {
    cyw43_arch_lwip_begin();
    release_client(true);
    complete(online ? "Weather HTTPS request timed out" : "Weather network/time unavailable");
    cyw43_arch_lwip_end();
  }
  if (done) {
    done = false;
    unsigned status = 0;
    const char *body = nullptr;
    size_t length = 0;
    if (!failure && !z80_weather_http_decode(wire, received, &status, &body, &length))
      failure = "Invalid or oversized weather HTTP response";
    z80_weather_result(status, body, length, failure);
  }
  z80_weather_poll(online);
}
