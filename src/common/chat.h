#ifndef Z80SBC_CHAT_H
#define Z80SBC_CHAT_H

#include "io_service.h"

enum {
  Z80_CHAT_REQUEST_MAX = 8192,
  Z80_CHAT_RESPONSE_BYTES = 4096,
  Z80_CHAT_EOF = 0, Z80_CHAT_WAITING = 1, Z80_CHAT_DATA_READY = 2,
};

typedef struct {
  bool https;
  uint16_t port;
  char host[96], authority[104], path[160], key[256];
} z80_chat_endpoint_t;

/* Core 1 only. The existing service mailbox is the sole core-0 boundary.
 * The request/endpoint stay immutable until cancel or network completion. */
void z80_chat_init(bool (*start)(const z80_chat_endpoint_t *, const char *, size_t),
                   void (*cancel)(void));
io_service_error_t z80_chat_service(const uint8_t *request, size_t length,
                                   io_service_result_t *result);
size_t z80_chat_receive(const uint8_t *data, size_t length);
bool z80_chat_active(void);
void z80_chat_error(const char *message);
void z80_chat_network_poll(bool online, uint32_t generation);
/* Stage 10 orchestration hook; implementation owns all lwIP/TLS resources. */
void z80_chat_network_service(bool online, uint32_t generation);

#endif
