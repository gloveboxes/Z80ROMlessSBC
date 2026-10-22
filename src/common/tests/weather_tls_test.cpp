#include <cassert>
#include <cstdio>
#include <ctime>

#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "weather_ca.h"
#include "chat_ca.h"

extern "C" time_t z80_network_time(time_t *result) {
  time_t now = time(nullptr);
  if (result)
    *result = now;
  return now;
}

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
  return static_cast<mbedtls_ms_time_t>(time(nullptr)) * 1000;
}

int main() {
  mbedtls_x509_crt ca;
  mbedtls_x509_crt_init(&ca);
  int result = mbedtls_x509_crt_parse(
      &ca, reinterpret_cast<const unsigned char *>(WEATHER_CA), sizeof(WEATHER_CA));
  if (result != 0) {
    char error[160];
    mbedtls_strerror(result, error, sizeof(error));
    fprintf(stderr, "Weather CA parse failed: -0x%04x %s\n", -result, error);
  }
  assert(result == 0);
  assert(ca.sig_md == MBEDTLS_MD_SHA384);
  uint32_t flags = 0;
  assert(mbedtls_x509_crt_verify(&ca, &ca, nullptr, nullptr, &flags,
                                 nullptr, nullptr) == 0);
  assert(flags == 0);
  mbedtls_ssl_config config;
  mbedtls_ssl_config_init(&config);
  assert(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
  mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&config, &ca, nullptr);
  mbedtls_ssl_config_free(&config);
  mbedtls_x509_crt_free(&ca);
  mbedtls_x509_crt_init(&ca);
  assert(mbedtls_x509_crt_parse(&ca,
      reinterpret_cast<const unsigned char *>(CHAT_CA), sizeof(CHAT_CA)) == 0);
  flags = 0;
  assert(mbedtls_x509_crt_verify(&ca, &ca, nullptr, nullptr, &flags,
                                 nullptr, nullptr) == 0 && flags == 0);
  mbedtls_x509_crt_free(&ca);
  puts("PASS: real firmware Mbed TLS configuration parses and verifies weather and chat CAs");
}
