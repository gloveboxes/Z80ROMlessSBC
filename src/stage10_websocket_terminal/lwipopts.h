#ifndef Z80SBC_LWIPOPTS_H
#define Z80SBC_LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE 65536
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG 40
#define MEMP_NUM_SYS_TIMEOUT 16
#define PBUF_POOL_SIZE 24
#define TCP_MSS 1460
/* altcp_mbedtls retains encrypted bytes until it can decrypt a complete TLS
 * record. A receive window below the 16 KiB input record can deadlock before
 * application callbacks have any bytes to consume and acknowledge. */
#define TCP_WND (12 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define ALTCP_MBEDTLS_AUTHMODE 2 /* MBEDTLS_SSL_VERIFY_REQUIRED */
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_STATS 0
#define SNTP_SERVER_DNS 1
#include "z80sbc/network_time.h"
#define SNTP_SET_SYSTEM_TIME(seconds) z80_io_ports_set_unix_time(seconds)

#endif
