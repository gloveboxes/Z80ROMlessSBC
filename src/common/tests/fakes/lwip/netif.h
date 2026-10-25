#ifndef TEST_LWIP_NETIF_H
#define TEST_LWIP_NETIF_H
#include <stdint.h>
struct ip4_addr { uint32_t address; };
struct netif { struct ip4_addr address; const char *hostname; };
extern struct netif *netif_default;
#define netif_ip4_addr(interface) (&(interface)->address)
char *ip4addr_ntoa_r(const struct ip4_addr *address, char *buffer, int capacity);
void netif_set_hostname(struct netif *interface, const char *hostname);
#endif
