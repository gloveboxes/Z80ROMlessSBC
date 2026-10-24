#ifndef TEST_MBEDTLS_SSL_H
#define TEST_MBEDTLS_SSL_H
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
struct mbedtls_ssl_context {};
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *, const char *);
#endif
