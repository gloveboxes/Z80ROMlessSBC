#ifndef TEST_DNS_H
#define TEST_DNS_H
#include "altcp_tls.h"
typedef void (*dns_found_callback)(const char *, const ip_addr_t *, void *);
err_t dns_gethostbyname(const char *, ip_addr_t *, dns_found_callback, void *);
#endif
