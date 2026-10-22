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
#include "mbedtls/platform_util.h"
#include "chat_ca.h"

extern "C" {
#include "../common/chat.h"
#include "z80sbc/network_time.h"
}

namespace {
constexpr uint32_t TIMEOUT_MS = 120000;
constexpr size_t WORK_BYTES = 512;
constexpr size_t RECEIVE_MAX = 16384;
altcp_tls_config *tls_config;
altcp_pcb *client;
pbuf *pending;
size_t pending_offset;
const z80_chat_endpoint_t *endpoint;
const char *body;
size_t body_length, body_sent, header_length, header_sent;
char header[768];
uint32_t generation;
bool initialized, active, handshake_ready, remote_closed;
const char *failure;
absolute_time_t deadline;

void release_client() {
  altcp_pcb *pcb = client;
  client = nullptr;
  if (pcb) {
    altcp_arg(pcb, nullptr);
    altcp_recv(pcb, nullptr);
    altcp_err(pcb, nullptr);
    altcp_sent(pcb, nullptr);
    altcp_poll(pcb, nullptr, 0);
    /* Teardown runs outside callbacks. No deferred close can outlive a request. */
    altcp_abort(pcb);
  }
  if (pending) {
    pbuf_free(pending);
    pending = nullptr;
  }
  pending_offset = 0;
  mbedtls_platform_zeroize(header, sizeof(header));
}

bool current(void *arg) {
  return active && reinterpret_cast<uintptr_t>(arg) == generation;
}

void socket_error(void *arg, err_t) {
  if (!current(arg))
    return;
  client = nullptr;
  failure = "DNS/TCP/TLS connection failed";
}

void send_request() {
  if (!handshake_ready || failure)
    return;
  const char *data = header_sent < header_length ? header + header_sent : body + body_sent;
  size_t remaining = header_sent < header_length ? header_length - header_sent :
                                                 body_length - body_sent;
  if (remaining) {
    u16_t length = static_cast<u16_t>(std::min(remaining, WORK_BYTES));
    err_t result = altcp_write(client, data, length, TCP_WRITE_FLAG_COPY);
    if (result == ERR_OK) {
      if (header_sent < header_length) header_sent += length;
      else body_sent += length;
    } else if (result != ERR_MEM) {
      failure = "HTTP request write failed";
      return;
    }
  }
  // Accepted bytes are never enqueued again, even if flushing runs out of RAM.
  err_t result = altcp_output(client);
  if (result != ERR_OK && result != ERR_MEM)
    failure = "HTTP request flush failed";
}

err_t connected(void *arg, altcp_pcb *pcb, err_t error) {
  if (!current(arg) || pcb != client)
    return ERR_OK;
  if (error != ERR_OK) {
    failure = "TCP/TLS connection failed";
    return ERR_OK;
  }
  handshake_ready = true;
  return ERR_OK;
}

err_t receive(void *arg, altcp_pcb *pcb, pbuf *data, err_t error) {
  if (!current(arg) || pcb != client) {
    if (data) pbuf_free(data);
    return ERR_OK;
  }
  if (!data) {
    remote_closed = true;
    if (error != ERR_OK)
      failure = "HTTP receive failed";
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(data);
    failure = "HTTP receive failed";
    return ERR_OK;
  }
  if (data->tot_len > RECEIVE_MAX) {
    pbuf_free(data);
    failure = "HTTP receive chain exceeds 16384 bytes";
    return ERR_OK;
  }
  if (pending)
    return ERR_MEM;
  /* Retain one chain. TCP credit is returned only as the parser consumes it;
   * later chains remain with lwIP on ERR_MEM, never silently dropped. */
  pending = data;
  pending_offset = 0;
  return ERR_OK;
}

void resolved(const char *, const ip_addr_t *address, void *arg) {
  if (!current(arg) || failure || !z80_chat_active())
    return;
  if (!address) {
    failure = "DNS lookup failed";
    return;
  }
  client = endpoint->https ? altcp_tls_new(tls_config, IPADDR_TYPE_V4) :
                             altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!client) {
    failure = "TCP/TLS allocation failed";
    return;
  }
  if (endpoint->https) {
    auto *ssl = static_cast<mbedtls_ssl_context *>(altcp_tls_context(client));
    if (!ssl || mbedtls_ssl_set_hostname(ssl, endpoint->host) != 0) {
      failure = "TLS hostname verification setup failed";
      return;
    }
  }
  altcp_arg(client, arg);
  altcp_recv(client, receive);
  altcp_err(client, socket_error);
  if (altcp_connect(client, address, endpoint->port, connected) != ERR_OK)
    failure = "TCP connection start failed";
}

void cancel() {
  cyw43_arch_lwip_begin();
  if (++generation == 0) ++generation;
  active = false;
  release_client();
  cyw43_arch_lwip_end();
}

bool start(const z80_chat_endpoint_t *target, const char *json, size_t length) {
  cancel();
  endpoint = target;
  body = json;
  body_length = length;
  body_sent = header_sent = 0;
  failure = nullptr;
  remote_closed = handshake_ready = false;
  int used = snprintf(header, sizeof(header),
      "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
      "Accept: text/event-stream\r\nAccept-Encoding: identity\r\n"
      "Connection: close\r\nContent-Length: %u\r\n%s%s%s\r\n",
      target->path, target->authority, static_cast<unsigned>(length),
      target->key[0] ? "Authorization: Bearer " : "", target->key,
      target->key[0] ? "\r\n" : "");
  if (used <= 0 || static_cast<size_t>(used) >= sizeof(header))
    return false;
  header_length = static_cast<size_t>(used);
  active = true;
  deadline = make_timeout_time_ms(TIMEOUT_MS);
  if (target->https && !z80_network_time(nullptr)) {
    failure = "TLS needs synchronized SNTP time";
    return true;
  }
  cyw43_arch_lwip_begin();
  if (target->https && !tls_config)
    tls_config = altcp_tls_create_config_client(
        reinterpret_cast<const u8_t *>(CHAT_CA), sizeof(CHAT_CA));
  if (target->https && !tls_config) {
    failure = "TLS trust configuration failed";
  } else {
    ip_addr_t address;
    void *token = reinterpret_cast<void *>(static_cast<uintptr_t>(generation));
    err_t result = dns_gethostbyname(target->host, &address, resolved, token);
    if (result == ERR_OK) resolved(target->host, &address, token);
    else if (result != ERR_INPROGRESS) failure = "DNS lookup start failed";
  }
  cyw43_arch_lwip_end();
  return true;
}
}  // namespace

extern "C" void z80_chat_network_service(bool online, uint32_t network_generation) {
  if (!initialized) {
    z80_chat_init(start, cancel);
    initialized = true;
  }
  z80_chat_network_poll(online, network_generation);
  if (!active)
    return;
  cyw43_arch_lwip_begin();
  if (z80_chat_active() && !failure && time_reached(deadline))
    failure = "HTTP stream timed out (120 seconds)";
  if (z80_chat_active() && !failure && client)
    send_request();
  if (z80_chat_active() && !failure && pending) {
    uint8_t bytes[WORK_BYTES];
    u16_t length = static_cast<u16_t>(
        std::min(WORK_BYTES, static_cast<size_t>(pending->tot_len) - pending_offset));
    if (pbuf_copy_partial(pending, bytes, length, static_cast<u16_t>(pending_offset)) != length) {
      failure = "HTTP receive chain copy failed";
    } else {
      size_t consumed = z80_chat_receive(bytes, length);
      pending_offset += consumed;
      if (consumed && client)
        altcp_recved(client, static_cast<u16_t>(consumed));
      if (pending_offset == pending->tot_len) {
        pbuf_free(pending);
        pending = nullptr;
        pending_offset = 0;
      }
    }
  }
  if (z80_chat_active() && remote_closed && !pending && !failure)
    failure = "Chat stream closed before [DONE]";
  if (failure && z80_chat_active())
    z80_chat_error(failure);
  if (!z80_chat_active()) {
    active = false;
    release_client();
  }
  cyw43_arch_lwip_end();
}
