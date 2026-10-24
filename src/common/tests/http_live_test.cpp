/* Optional native integration runner: real sockets and the firmware Mbed TLS
 * configuration/trust bundle, feeding the production parser and FT ports.
 * The host socket backend blocks; the Pico backend is tested separately. */
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/socket.h>
#include <sys/random.h>
#include <netdb.h>
#include <unistd.h>

#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "http_ca.h"
extern "C" {
#include "http_io.h"
}

static int connection = -1;
static bool secure;
static unsigned transfer_generation;
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config config;
static mbedtls_x509_crt roots;

extern "C" time_t z80_network_time(time_t *out) { return time(out); }
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
extern "C" void environment_io_init(const char *) {}
extern "C" void environment_io_close(void) {}
extern "C" uint8_t environment_input(uint8_t) { return 0; }
extern "C" bool environment_io_get(const char *, char *, size_t) { return false; }

static int random_bytes(void *, unsigned char *data, size_t n) {
  while (n) {
    size_t used = std::min(n, size_t(256));
    if (getentropy(data, used) != 0) return -1;
    data += used; n -= used;
  }
  return 0;
}
static int send_bytes(void *, const unsigned char *data, size_t n) {
  ssize_t result = send(connection, data, n, 0);
  return result < 0 ? -1 : static_cast<int>(result);
}
static int recv_bytes(void *, unsigned char *data, size_t n) {
  ssize_t result = recv(connection, data, n, 0);
  return result < 0 ? -1 : static_cast<int>(result);
}
static void cancel() {
  if (connection >= 0) close(connection);
  connection = -1;
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_init(&ssl);
}
static bool start(const z80_http_url_t *url) {
  cancel();
  ++transfer_generation;
  secure = url->https;
  addrinfo hints = {}, *addresses = nullptr;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  std::string port = std::to_string(url->port);
  if (getaddrinfo(url->host, port.c_str(), &hints, &addresses) != 0) return false;
  for (addrinfo *a = addresses; a; a = a->ai_next) {
    connection = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (connection < 0) continue;
    timeval timeout = {15, 0};
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(connection, a->ai_addr, a->ai_addrlen) == 0) break;
    close(connection); connection = -1;
  }
  freeaddrinfo(addresses);
  if (connection < 0) return false;
  if (secure) {
    assert(mbedtls_ssl_setup(&ssl, &config) == 0);
    assert(mbedtls_ssl_set_hostname(&ssl, url->host) == 0);
    mbedtls_ssl_set_bio(&ssl, nullptr, send_bytes, recv_bytes, nullptr);
    int result = mbedtls_ssl_handshake(&ssl);
    if (result != 0) {
      char error[256]; mbedtls_strerror(result, error, sizeof(error));
      fprintf(stderr, "TLS handshake: %s (-0x%x)\n", error, -result);
      return false;
    }
    assert(mbedtls_ssl_get_verify_result(&ssl) == 0);
  }
  std::string request = std::string("GET ") + url->path + " HTTP/1.1\r\nHost: " +
      url->authority + "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
  size_t offset = 0;
  while (offset < request.size()) {
    auto *bytes = reinterpret_cast<const unsigned char *>(request.data() + offset);
    int result = secure ? mbedtls_ssl_write(&ssl, bytes, request.size() - offset) :
                          send_bytes(nullptr, bytes, request.size() - offset);
    if (result <= 0) return false;
    offset += static_cast<size_t>(result);
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: http_live_test URL OUTPUT\n"); return 2; }
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&config);
  mbedtls_x509_crt_init(&roots);
  assert(mbedtls_x509_crt_parse(&roots,
      reinterpret_cast<const unsigned char *>(HTTP_CA), sizeof(HTTP_CA)) == 0);
  assert(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
  mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&config, &roots, nullptr);
  mbedtls_ssl_conf_rng(&config, random_bytes, nullptr);
  z80_http_io_init();
  z80_http_io_backend(start, cancel);
  z80_http_io_network_state(true, 1);
  z80_http_io_out(21, HTTP_SET_FILENAME);
  for (const char *p = argv[1]; *p; ++p) z80_http_io_out(22, static_cast<uint8_t>(*p));
  z80_http_io_out(22, 0);
  z80_http_io_out(21, HTTP_REQUEST_CHUNK);
  FILE *file = fopen(argv[2], "wb"); assert(file);
  size_t total = 0;
  unsigned char bytes[997];
  size_t received = 0, offset = 0;
  unsigned previous_generation = 0;
  for (unsigned loops = 0; loops < 1000000; ++loops) {
    z80_http_io_core1_service();
    if (transfer_generation != previous_generation) {
      received = offset = 0;
      previous_generation = transfer_generation;
    }
    uint8_t status = z80_http_io_in(21);
    if (status == HTTP_ERROR) { fclose(file); cancel(); return 1; }
    if (status == HTTP_EOF) break;
    if (status == HTTP_DATA_READY) {
      size_t n = z80_http_io_in(22); if (!n) n = 256;
      for (size_t i = 0; i < n; ++i) assert(fputc(z80_http_io_in(22), file) != EOF);
      total += n;
      if (z80_http_io_in(21) == HTTP_EOF) break;
      z80_http_io_out(21, HTTP_REQUEST_CHUNK);
    }
    if (z80_http_io_active()) {
      if (offset == received) {
        int result = secure ? mbedtls_ssl_read(&ssl, bytes, sizeof(bytes)) :
                              recv_bytes(nullptr, bytes, sizeof(bytes));
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
          z80_http_io_remote_close(); continue;
        }
        assert(result > 0);
        received = static_cast<size_t>(result); offset = 0;
      }
      offset += z80_http_io_receive(bytes + offset, received - offset);
    }
  }
  assert(z80_http_io_in(21) == HTTP_EOF);
  z80_http_io_out(21, HTTP_CLOSE);
  z80_http_io_core1_service();
  fclose(file); cancel();
  mbedtls_ssl_config_free(&config); mbedtls_x509_crt_free(&roots);
  fprintf(stderr, "PASS: %zu bytes via firmware FT ports and %s\n", total, secure ? "verified TLS 1.2" : "HTTP");
}
