#ifndef Z80SBC_HTTP_STREAM_H
#define Z80SBC_HTTP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { Z80_HTTP_URL_MAX = 768 };
typedef struct {
  bool https;
  uint16_t port;
  char host[254], authority[260], path[Z80_HTTP_URL_MAX];
} z80_http_url_t;

/* DNS/IPv4 URLs, optional port; fragments, credentials and control bytes fail. */
bool z80_http_url_parse(const char *text, z80_http_url_t *url);
bool z80_http_url_redirect(z80_http_url_t *url, const char *location);

typedef struct {
  unsigned state, status, header_bytes;
  uint32_t remaining;
  size_t line_length;
  bool have_length, chunked, done;
  const char *error;
  char line[1024], location[Z80_HTTP_URL_MAX];
} z80_http_stream_t;

void z80_http_stream_init(z80_http_stream_t *stream);
/* Returns consumed wire bytes; stops before a body byte when emit is full. */
size_t z80_http_stream_feed(z80_http_stream_t *stream, const uint8_t *data,
                            size_t length, bool (*emit)(uint8_t));
void z80_http_stream_close(z80_http_stream_t *stream);

#ifdef __cplusplus
}
#endif
#endif
