#ifndef TEST_MBEDTLS_PLATFORM_UTIL_H
#define TEST_MBEDTLS_PLATFORM_UTIL_H
#include <stdint.h>
#include <stddef.h>
typedef int64_t mbedtls_ms_time_t;
void mbedtls_platform_zeroize(void *, size_t);
#endif
