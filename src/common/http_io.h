#ifndef Z80SBC_HTTP_IO_H
#define Z80SBC_HTTP_IO_H

#include "http_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  HTTP_COMMAND_PORT = 21, HTTP_DATA_PORT = 22,
  HTTP_IDLE = 0, HTTP_DATA_READY = 1, HTTP_EOF = 2, HTTP_BUSY = 3,
  HTTP_MISSING_BASE_URL = 4, HTTP_ERROR = 255,
  HTTP_SET_FILENAME = 1, HTTP_REQUEST_CHUNK = 3, HTTP_CLOSE = 4,
};
/* Core 0: bounded port operations; no network, ENV, waits or allocation. */
void z80_http_io_init(void);
void z80_http_io_out(uint8_t port, uint8_t data);
uint8_t z80_http_io_in(uint8_t port);

/* Core 1: install an asynchronous backend, then service once per worker loop. */
void z80_http_io_backend(bool (*start)(const z80_http_url_t *), void (*cancel)(void));
void z80_http_io_core1_service(void);
void z80_http_io_network_state(bool online, uint32_t generation);
bool z80_http_io_active(void);
size_t z80_http_io_receive(const uint8_t *data, size_t length);
void z80_http_io_remote_close(void);
void z80_http_io_error(const char *message);
void z80_http_network_service(bool online, uint32_t generation);

#ifdef __cplusplus
}
#endif
#endif
