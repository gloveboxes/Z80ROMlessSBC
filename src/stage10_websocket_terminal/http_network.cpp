#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "mbedtls/ssl.h"
#include "lwipopts.h"
#include "http_ca.h"

extern "C" {
#include "../common/http_io.h"
#include "z80sbc/network_time.h"
}

namespace {
constexpr uint32_t TIMEOUT_MS = 30000;
constexpr size_t WORK_BYTES = 512;
constexpr size_t RECEIVE_MAX = TCP_WND + MBEDTLS_SSL_IN_CONTENT_LEN;
static_assert(TCP_WND >= MBEDTLS_SSL_IN_CONTENT_LEN + 512,
              "TCP window must accommodate a complete TLS input record");
static_assert(RECEIVE_MAX <= 65535, "pbuf chain length must fit u16_t");
altcp_tls_config *tls_config;
altcp_pcb *client;
pbuf *pending;
size_t header_length, header_sent;
char header[1280];
/* Keep scratch storage off the Pico's small core 1 stack. */
uint8_t receive_work[WORK_BYTES];
const z80_http_url_t *endpoint;
uint32_t generation;
bool initialized, active, handshake_ready, remote_closed;
const char *failure;
absolute_time_t deadline;

bool current(void *arg) {
  return active && reinterpret_cast<uintptr_t>(arg) == generation;
}

void cancel() {
  cyw43_arch_lwip_begin();
  active = false;
  if (++generation == 0) ++generation;
  if (client) {
    altcp_arg(client, nullptr);
    altcp_recv(client, nullptr);
    altcp_err(client, nullptr);
    altcp_sent(client, nullptr);
    altcp_poll(client, nullptr, 0);
    altcp_abort(client);
    client = nullptr;
  }
  if (pending) pbuf_free(pending);
  pending = nullptr;
  cyw43_arch_lwip_end();
}

void socket_error(void *arg, err_t) {
  if (!current(arg)) return;
  client = nullptr;
  failure = "HTTP DNS/TCP/TLS connection failed";
}

err_t connected(void *arg, altcp_pcb *pcb, err_t error) {
  if (!current(arg) || pcb != client) return ERR_OK;
  if (error != ERR_OK) {
    failure = "HTTP TCP/TLS connection failed";
  }
  else {
    handshake_ready = true;
    deadline = make_timeout_time_ms(TIMEOUT_MS);
  }
  return ERR_OK;
}

err_t receive(void *arg, altcp_pcb *pcb, pbuf *data, err_t error) {
  if (!current(arg) || pcb != client || failure) {
    if (data) pbuf_free(data);
    return ERR_OK;
  }
  if (!data) {
    remote_closed = true;
    if (error != ERR_OK) {
      failure = "HTTP receive failed";
    }
    return ERR_OK;
  }
  if (!data->tot_len) {
    pbuf_free(data);
    return ERR_OK;
  }
  if (error != ERR_OK || data->tot_len > RECEIVE_MAX ||
      (pending && data->tot_len > RECEIVE_MAX - pending->tot_len)) {
    pbuf_free(data);
    failure = "HTTP receive failed or backlog exceeds TCP/TLS bound";
    return ERR_OK;
  }
  if (pending) {
    pbuf_cat(pending, data);
  } else pending = data;
  /* Keep successive decrypted buffers in one bounded chain instead of
   * refusing a final plaintext fragment to the TLS wrapper. TCP credit is
   * withheld until the normal worker consumes bytes; no payload is copied or
   * parsed in this callback. Fully consumed pbufs are released incrementally. */
  deadline = make_timeout_time_ms(TIMEOUT_MS);
  return ERR_OK;
}

err_t poll(void *, altcp_pcb *) {
  /* Installing this timer enables altcp_mbedtls_lower_poll, which retries
   * TLS output/receive work after temporary memory pressure. Worker polling
   * alone cannot advance those internal queues. Keep application work on core 1's
   * ordinary service path rather than performing it in this callback. */
  return ERR_OK;
}

void resolved(const char *, const ip_addr_t *address, void *arg) {
  if (!current(arg) || failure || !z80_http_io_active()) return;
  if (!address) { failure = "HTTP DNS lookup failed"; return; }
  client = endpoint->https ? altcp_tls_new(tls_config, IPADDR_TYPE_V4) :
                             altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!client) { failure = "HTTP TCP/TLS allocation failed"; return; }
  if (endpoint->https) {
    auto *ssl = static_cast<mbedtls_ssl_context *>(altcp_tls_context(client));
    if (!ssl || mbedtls_ssl_set_hostname(ssl, endpoint->host) != 0) {
      failure = "HTTP TLS hostname verification setup failed";
      return;
    }
  }
  altcp_arg(client, arg);
  altcp_recv(client, receive);
  altcp_err(client, socket_error);
  altcp_poll(client, poll, 1);
  err_t result = altcp_connect(client, address, endpoint->port, connected);
  if (result != ERR_OK) {
    failure = "HTTP connection start failed";
  }
}

bool start(const z80_http_url_t *target) {
  cancel();
  endpoint = target;
  header_sent = 0;
  failure = nullptr;
  remote_closed = handshake_ready = false;
  int used = snprintf(header, sizeof(header),
      "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: */*\r\n"
      "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
      endpoint->path, endpoint->authority);
  if (used <= 0 || static_cast<size_t>(used) >= sizeof(header)) return false;
  header_length = static_cast<size_t>(used);
  active = true;
  deadline = make_timeout_time_ms(TIMEOUT_MS);
  if (target->https && !z80_network_time(nullptr)) {
    failure = "HTTP TLS needs synchronized SNTP time";
    return true;
  }
  cyw43_arch_lwip_begin();
  if (target->https && !tls_config)
    tls_config = altcp_tls_create_config_client(
        reinterpret_cast<const u8_t *>(HTTP_CA), sizeof(HTTP_CA));
  if (target->https && !tls_config) failure = "HTTP TLS trust configuration failed";
  else {
    ip_addr_t address;
    void *token = reinterpret_cast<void *>(static_cast<uintptr_t>(generation));
    err_t result = dns_gethostbyname(target->host, &address, resolved, token);
    if (result == ERR_OK) resolved(target->host, &address, token);
    else if (result != ERR_INPROGRESS) {
      failure = "HTTP DNS lookup start failed";
    }
  }
  cyw43_arch_lwip_end();
  return true;
}
}  // namespace

extern "C" void z80_http_network_service(bool online, uint32_t network_generation) {
  if (!initialized) {
    z80_http_io_backend(start, cancel);
    initialized = true;
  }
  z80_http_io_network_state(online, network_generation);
  z80_http_io_core1_service();
  if (!active) return;
  cyw43_arch_lwip_begin();
  if (!failure && !pending && time_reached(deadline)) failure = "HTTP transfer idle timeout (30 seconds)";
  if (!failure && client && handshake_ready) {
    if (header_sent < header_length) {
      u16_t n = static_cast<u16_t>(std::min(WORK_BYTES, header_length - header_sent));
      err_t result = altcp_write(client, header + header_sent, n, TCP_WRITE_FLAG_COPY);
      if (result == ERR_OK) {
        header_sent += n;
      } else if (result != ERR_MEM) {
        failure = "HTTP request write failed";
      }
    }
    err_t result = altcp_output(client);
    if (result != ERR_OK && result != ERR_MEM) {
      failure = "HTTP request flush failed";
    }
  }
  if (!failure && pending) {
    u16_t n = static_cast<u16_t>(std::min(WORK_BYTES, static_cast<size_t>(pending->tot_len)));
    if (pbuf_copy_partial(pending, receive_work, n, 0) != n)
      failure = "HTTP receive chain copy failed";
    else {
      size_t used = z80_http_io_receive(receive_work, n);
      if (used) {
        pending = pbuf_free_header(pending, static_cast<u16_t>(used));
        if (client) altcp_recved(client, static_cast<u16_t>(used));
      }
      /* A full consumer ring is deliberate backpressure, not a network stall. */
      deadline = make_timeout_time_ms(TIMEOUT_MS);
    }
  }
  if (!failure && remote_closed && !pending) z80_http_io_remote_close();
  cyw43_arch_lwip_end();
  if (failure) z80_http_io_error(failure);
  /* Cancels/redirects outside lwIP callbacks and outside the stack guard. */
  z80_http_io_core1_service();
}
