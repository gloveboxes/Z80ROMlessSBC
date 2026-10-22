#include "chat_stream.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { STATUS_LINE, HEADERS, BODY };
enum { CHUNK_SIZE, CHUNK_SIZE_LF, CHUNK_DATA, CHUNK_CR, CHUNK_LF, TRAILERS };

static int hex(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static uint8_t ascii(uint32_t cp) {
  if (cp == '\n' || cp == '\r' || cp == '\t' || (cp >= 32 && cp < 127))
    return (uint8_t)cp;
  if (cp == 0xa0) return ' ';
  if (cp == 0x2018 || cp == 0x2019) return '\'';
  if (cp == 0x201c || cp == 0x201d) return '"';
  if (cp == 0x2013 || cp == 0x2014) return '-';
  return '?';
}

typedef struct {
  const char *p, *end;
  z80_chat_stream_t *stream;
  bool content_seen, provider_error;
} json_t;

static void whitespace(json_t *j) {
  while (j->p < j->end && strchr(" \t\r\n", *j->p))
    ++j->p;
}

static bool unicode_escape(json_t *j, uint32_t *cp) {
  *cp = 0;
  for (unsigned i = 0; i < 4; ++i) {
    if (j->p == j->end || hex((unsigned char)*j->p) < 0)
      return false;
    *cp = (*cp << 4) | (unsigned)hex((unsigned char)*j->p++);
  }
  return true;
}

static bool string(json_t *j, char *out, size_t capacity, size_t *length) {
  if (j->p == j->end || *j->p++ != '"')
    return false;
  size_t used = 0;
  while (j->p < j->end) {
    uint32_t cp = (unsigned char)*j->p++;
    if (cp == '"') {
      if (out)
        out[used < capacity ? used : capacity - 1] = '\0';
      *length = used;
      return true;
    }
    if (cp < 32)
      return false;
    if (cp == '\\') {
      if (j->p == j->end)
        return false;
      unsigned char escape = (unsigned char)*j->p++;
      if (escape == 'u') {
        if (!unicode_escape(j, &cp))
          return false;
        if (cp >= 0xd800 && cp <= 0xdbff) {
          uint32_t low;
          if (j->end - j->p < 6 || *j->p++ != '\\' || *j->p++ != 'u' ||
              !unicode_escape(j, &low) || low < 0xdc00 || low > 0xdfff)
            return false;
          cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
          return false;
        }
      } else {
        const char *escapes = "\"\\/bfnrt";
        const char *found = strchr(escapes, escape);
        if (!found)
          return false;
        cp = (const unsigned char[]){'"', '\\', '/', '\b', '\f', '\n', '\r', '\t'}
             [found - escapes];
      }
    } else if (cp >= 128) {
      unsigned extra;
      uint32_t minimum;
      if (cp >= 0xc2 && cp <= 0xdf) {
        extra = 1; minimum = 0x80; cp &= 0x1f;
      } else if (cp >= 0xe0 && cp <= 0xef) {
        extra = 2; minimum = 0x800; cp &= 0x0f;
      } else if (cp >= 0xf0 && cp <= 0xf4) {
        extra = 3; minimum = 0x10000; cp &= 7;
      } else {
        return false;
      }
      while (extra--) {
        if (j->p == j->end || ((unsigned char)*j->p & 0xc0) != 0x80)
          return false;
        cp = (cp << 6) | ((unsigned char)*j->p++ & 0x3f);
      }
      if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return false;
    }
    if (out && used + 1 < capacity)
      out[used] = (char)ascii(cp);
    ++used;
  }
  return false;
}

/* Select only choices[0].delta.content. Validate skipped JSON as well, so
 * quoted keys/reasoning/tool arguments can never be mistaken for content. */
static bool value(json_t *j, unsigned depth, unsigned path) {
  if (depth > 16)
    return false;
  whitespace(j);
  if (j->p == j->end)
    return false;
  if (*j->p == '{') {
    ++j->p;
    whitespace(j);
    if (j->p < j->end && *j->p == '}') { ++j->p; return true; }
    for (;;) {
      char key[32];
      size_t length;
      if (!string(j, key, sizeof(key), &length))
        return false;
      whitespace(j);
      if (j->p == j->end || *j->p++ != ':')
        return false;
      unsigned next = 0;
      if (length < sizeof(key)) {
        if (path == 1 && strcmp(key, "choices") == 0) next = 2;
        if (path == 3 && strcmp(key, "delta") == 0) next = 4;
        if (path == 4 && strcmp(key, "content") == 0) next = 5;
        if (path == 1 && strcmp(key, "error") == 0) j->provider_error = true;
      }
      if (!value(j, depth + 1, next))
        return false;
      whitespace(j);
      if (j->p == j->end)
        return false;
      char c = *j->p++;
      if (c == '}') return true;
      if (c != ',') return false;
      whitespace(j);
    }
  }
  if (*j->p == '[') {
    ++j->p;
    whitespace(j);
    if (j->p < j->end && *j->p == ']') { ++j->p; return true; }
    unsigned index = 0;
    for (;;) {
      if (!value(j, depth + 1, path == 2 && index == 0 ? 3 : 0))
        return false;
      ++index;
      whitespace(j);
      if (j->p == j->end)
        return false;
      char c = *j->p++;
      if (c == ']') return true;
      if (c != ',') return false;
    }
  }
  if (*j->p == '"') {
    size_t length;
    bool capture = path == 5;
    if (capture && j->content_seen)
      return false;
    if (!string(j, capture ? j->stream->text : NULL,
                sizeof(j->stream->text), &length))
      return false;
    if (capture) {
      if (length >= sizeof(j->stream->text))
        return false;
      j->stream->text_length = length;
      j->content_seen = true;
    }
    return true;
  }
  const char *start = j->p;
  const char *literal = *start == 't' ? "true" : *start == 'f' ? "false" :
                        *start == 'n' ? "null" : NULL;
  if (literal) {
    size_t length = strlen(literal);
    if ((size_t)(j->end - j->p) < length || memcmp(j->p, literal, length))
      return false;
    j->p += length;
    return true;
  }
  if (*j->p == '-') ++j->p;
  if (j->p == j->end) return false;
  if (*j->p == '0') ++j->p;
  else {
    if (*j->p < '1' || *j->p > '9') return false;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') ++j->p;
  }
  if (j->p < j->end && *j->p == '.') {
    ++j->p;
    const char *digits = j->p;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') ++j->p;
    if (digits == j->p) return false;
  }
  if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
    ++j->p;
    if (j->p < j->end && (*j->p == '+' || *j->p == '-')) ++j->p;
    const char *digits = j->p;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') ++j->p;
    if (digits == j->p) return false;
  }
  return j->p > start;
}

static void event(z80_chat_stream_t *s) {
  if (strncmp(s->line, "data:", 5) != 0)
    return;
  const char *data = s->line + 5;
  if (*data == ' ') ++data;
  if (strcmp(data, "[DONE]") == 0) {
    s->done = true;
    return;
  }
  json_t j = {data, s->line + s->line_length, s, false, false};
  s->text_length = s->text_position = 0;
  whitespace(&j);
  if (j.p == j.end || *j.p != '{' || !value(&j, 0, 1)) {
    s->error = "Malformed chat SSE JSON";
    return;
  }
  whitespace(&j);
  if (j.p != j.end)
    s->error = "Trailing data in chat SSE JSON";
  else if (j.provider_error)
    s->error = "Provider reported a streaming error";
}

static bool equal_ci(const char *a, const char *b) {
  while (*a && *b) {
    unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
    if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
    if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
    if (x != y) return false;
  }
  return *a == *b;
}

static void header(z80_chat_stream_t *s) {
  char *colon = strchr(s->line, ':');
  if (!colon) { s->error = "Malformed chat HTTP header"; return; }
  *colon++ = '\0';
  while (*colon == ' ' || *colon == '\t') ++colon;
  char *end = colon + strlen(colon);
  while (end > colon && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
  if (equal_ci(s->line, "Transfer-Encoding")) {
    if (s->chunked || s->have_length || !equal_ci(colon, "chunked"))
      s->error = "Unsupported chat HTTP transfer encoding";
    s->chunked = true;
  } else if (equal_ci(s->line, "Content-Length")) {
    if (s->have_length || s->chunked || !*colon) {
      s->error = "Invalid chat HTTP content length";
      return;
    }
    uint32_t n = 0;
    for (; *colon; ++colon) {
      if (*colon < '0' || *colon > '9' || n > (UINT32_MAX - 9) / 10) {
        s->error = "Invalid chat HTTP content length"; return;
      }
      n = n * 10 + (unsigned)(*colon - '0');
    }
    s->remaining = n;
    s->have_length = true;
  } else if (equal_ci(s->line, "Content-Type")) {
    char *parameter = strchr(colon, ';');
    if (parameter) *parameter = '\0';
    s->event_stream = equal_ci(colon, "text/event-stream");
  } else if (equal_ci(s->line, "Content-Encoding") && !equal_ci(colon, "identity")) {
    s->error = "Unsupported compressed chat response";
  }
}

static void line(z80_chat_stream_t *s, uint8_t byte) {
  if (!byte) { s->error = "NUL in chat HTTP/SSE"; return; }
  if (byte != '\n') {
    if (s->line_length + 1 == sizeof(s->line)) {
      s->error = "Chat HTTP/SSE line exceeds 4095 bytes"; return;
    }
    s->line[s->line_length++] = (char)byte;
    return;
  }
  if (s->line_length && s->line[s->line_length - 1] == '\r')
    --s->line_length;
  s->line[s->line_length] = '\0';
  if (s->phase == STATUS_LINE) {
    int used = 0;
    if (sscanf(s->line, "HTTP/1.%*1[01] %3u%n", &s->status, &used) != 1 ||
        used != 12 || (s->line[used] && s->line[used] != ' '))
      s->error = "Invalid chat HTTP status";
    else if (s->status != 200)
      s->error = s->status == 401 || s->status == 403 ?
          "HTTP authentication rejected (401/403)" :
          s->status == 429 ? "HTTP rate limit exceeded (429)" :
          "Chat HTTP status is not 200";
    s->phase = HEADERS;
  } else if (s->phase == HEADERS) {
    if (s->line_length)
      header(s);
    else if (!s->event_stream)
      s->error = "Chat response is not text/event-stream";
    else
      s->phase = BODY;
  } else if (s->chunk_phase == TRAILERS && s->chunked) {
    if (!s->line_length)
      s->error = "Chat stream ended without [DONE]";
  } else if (s->line_length) {
    event(s);
  }
  s->line_length = 0;
}

void z80_chat_stream_init(z80_chat_stream_t *s) {
  memset(s, 0, sizeof(*s));
}

size_t z80_chat_stream_feed(z80_chat_stream_t *s, const uint8_t *data,
                            size_t length, bool (*emit)(uint8_t)) {
  size_t used = 0;
  for (;;) {
    while (!s->error && s->text_position < s->text_length) {
      if (!emit((uint8_t)s->text[s->text_position]))
        return used;
      ++s->text_position;
    }
    if (s->error || s->done || used == length)
      return used;
    uint8_t c = data[used++];
    if (s->phase != BODY) {
      if (++s->header_bytes > 4096) s->error = "Chat HTTP headers exceed 4096 bytes";
      else line(s, c);
    } else if (!s->chunked) {
      if (s->have_length && !s->remaining) {
        s->error = "Chat stream ended without [DONE]"; break;
      }
      line(s, c);
      if (s->have_length && --s->remaining == 0 && !s->done)
        s->error = "Chat stream ended without [DONE]";
    } else if (s->chunk_phase == CHUNK_SIZE) {
      if (c == '\r' && s->chunk_digits) s->chunk_phase = CHUNK_SIZE_LF;
      else if (c == ';' && s->chunk_digits) s->extension = true;
      else if (s->extension && c >= 32 && c < 127) {
        if (++s->header_bytes > 4096) s->error = "Chat chunk extensions too large";
      } else if (!s->extension && hex(c) >= 0 && s->chunk_digits < 8) {
        s->chunk_size = (s->chunk_size << 4) | (unsigned)hex(c);
        ++s->chunk_digits;
      } else s->error = "Invalid chat HTTP chunk size";
    } else if (s->chunk_phase == CHUNK_SIZE_LF) {
      if (c != '\n') s->error = "Invalid chat HTTP chunk delimiter";
      else s->chunk_phase = s->chunk_size ? CHUNK_DATA : TRAILERS;
    } else if (s->chunk_phase == CHUNK_DATA) {
      line(s, c);
      if (--s->chunk_size == 0) s->chunk_phase = CHUNK_CR;
    } else if (s->chunk_phase == CHUNK_CR) {
      if (c != '\r') s->error = "Invalid chat HTTP chunk delimiter";
      else s->chunk_phase = CHUNK_LF;
    } else if (s->chunk_phase == CHUNK_LF) {
      if (c != '\n') s->error = "Invalid chat HTTP chunk delimiter";
      else {
        s->chunk_phase = CHUNK_SIZE;
        s->chunk_digits = 0;
        s->extension = false;
      }
    } else {
      if (++s->header_bytes > 8192) s->error = "Chat HTTP trailers too large";
      else line(s, c);
    }
  }
  return used;
}
