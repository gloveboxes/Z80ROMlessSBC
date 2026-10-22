#ifndef Z80SBC_CHAT_STREAM_H
#define Z80SBC_CHAT_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { Z80_CHAT_LINE_MAX = 4096 };
typedef struct {
  char line[Z80_CHAT_LINE_MAX], text[Z80_CHAT_LINE_MAX];
  size_t line_length, text_length, text_position, header_bytes;
  uint32_t remaining, chunk_size;
  unsigned phase, chunk_phase, status;
  bool chunked, have_length, event_stream, done, extension;
  unsigned chunk_digits;
  const char *error;
} z80_chat_stream_t;

void z80_chat_stream_init(z80_chat_stream_t *stream);
/* Stops without consuming further input when the output callback is full. */
size_t z80_chat_stream_feed(z80_chat_stream_t *stream, const uint8_t *data,
                            size_t length, bool (*emit)(uint8_t));

#endif
