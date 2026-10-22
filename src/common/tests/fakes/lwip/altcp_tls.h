#ifndef TEST_ALTCP_TLS_H
#define TEST_ALTCP_TLS_H
#include <stddef.h>
#include <stdint.h>
typedef int err_t;
typedef uint8_t u8_t;
typedef uint16_t u16_t;
enum { ERR_OK = 0, ERR_MEM = -1, ERR_INPROGRESS = -5, ERR_ABRT = -13 };
enum { IPADDR_TYPE_V4 = 0, TCP_WRITE_FLAG_COPY = 1 };
struct ip_addr_t { uint32_t address; };
struct pbuf { const void *payload; u16_t tot_len; };
struct altcp_tls_config {};
struct altcp_pcb {
  void *arg;
  err_t (*recv)(void *, altcp_pcb *, pbuf *, err_t);
  err_t (*connected)(void *, altcp_pcb *, err_t);
  err_t (*sent)(void *, altcp_pcb *, u16_t);
  err_t (*poll)(void *, altcp_pcb *);
  void (*error)(void *, err_t);
};
altcp_tls_config *altcp_tls_create_config_client(const u8_t *, size_t);
altcp_pcb *altcp_tls_new(altcp_tls_config *, u8_t);
void *altcp_tls_context(altcp_pcb *);
void altcp_arg(altcp_pcb *, void *);
void altcp_recv(altcp_pcb *, err_t (*)(void *, altcp_pcb *, pbuf *, err_t));
void altcp_err(altcp_pcb *, void (*)(void *, err_t));
void altcp_sent(altcp_pcb *, err_t (*)(void *, altcp_pcb *, u16_t));
void altcp_poll(altcp_pcb *, err_t (*)(void *, altcp_pcb *), u8_t);
err_t altcp_close(altcp_pcb *);
void altcp_abort(altcp_pcb *);
err_t altcp_connect(altcp_pcb *, const ip_addr_t *, u16_t,
                    err_t (*)(void *, altcp_pcb *, err_t));
err_t altcp_write(altcp_pcb *, const void *, u16_t, u8_t);
err_t altcp_output(altcp_pcb *);
void altcp_recved(altcp_pcb *, u16_t);
u16_t pbuf_copy_partial(const pbuf *, void *, u16_t, u16_t);
u8_t pbuf_free(pbuf *);
#endif
