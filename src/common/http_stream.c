#include "http_stream.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

bool z80_http_url_parse(const char *text, z80_http_url_t *url) {
  memset(url, 0, sizeof(*url));
  const char *host;
  if (!strncasecmp(text, "https://", 8)) {
    url->https = true;
    url->port = 443;
    host = text + 8;
  } else if (!strncasecmp(text, "http://", 7)) {
    url->port = 80;
    host = text + 7;
  } else return false;
  for (const char *p = host; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 ||
        *p == '#' || *p == '\\' || *p == '@') return false;
  const char *end = host + strcspn(host, "/?");
  if (end == host || (size_t)(end - host) >= sizeof(url->authority)) return false;
  memcpy(url->authority, host, (size_t)(end - host));
  const char *colon = memchr(host, ':', (size_t)(end - host));
  size_t n = (size_t)((colon ? colon : end) - host);
  if (!n || n >= sizeof(url->host)) return false;
  for (size_t i = 0; i < n; ++i)
    if (!((host[i] >= 'a' && host[i] <= 'z') ||
          (host[i] >= 'A' && host[i] <= 'Z') ||
          (host[i] >= '0' && host[i] <= '9') || host[i] == '.' || host[i] == '-'))
      return false;
  memcpy(url->host, host, n);
  if (colon) {
    unsigned port = 0;
    if (colon + 1 == end) return false;
    for (const char *p = colon + 1; p < end; ++p) {
      if (*p < '0' || *p > '9' || port > 6553) return false;
      port = port * 10 + (unsigned)(*p - '0');
      if (port > 65535) return false;
    }
    if (!port) return false;
    url->port = (uint16_t)port;
  }
  int used = snprintf(url->path, sizeof(url->path), "%s%s",
                      *end == '/' ? "" : "/", end);
  return used > 0 && (size_t)used < sizeof(url->path);
}

bool z80_http_url_redirect(z80_http_url_t *url, const char *location) {
  char text[Z80_HTTP_URL_MAX];
  int n;
  if (!strncasecmp(location, "http://", 7) || !strncasecmp(location, "https://", 8))
    n = snprintf(text, sizeof(text), "%s", location);
  else if (!strncmp(location, "//", 2))
    n = snprintf(text, sizeof(text), "%s:%s", url->https ? "https" : "http", location);
  else {
    size_t prefix = strcspn(url->path, "?");
    if (*location == '/') prefix = 0;
    else if (*location != '?') {
      while (prefix && url->path[prefix - 1] != '/') --prefix;
    }
    n = snprintf(text, sizeof(text), "%s://%s%.*s%s", url->https ? "https" : "http",
                 url->authority, (int)prefix, url->path, location);
  }
  if (n <= 0 || (size_t)n >= sizeof(text)) return false;
  bool secure = url->https;
  return z80_http_url_parse(text, url) && (!secure || url->https);
}

enum { STATUS, HEADERS, BODY_LENGTH, BODY_CLOSE, CHUNK_SIZE, CHUNK_BODY,
       CHUNK_CR, CHUNK_LF, TRAILERS };

void z80_http_stream_init(z80_http_stream_t *s) { memset(s, 0, sizeof(*s)); }

static bool number(const char *p, unsigned base, uint32_t *result) {
  *result = 0;
  const char *first = p;
  for (; *p && !(base == 16 && *p == ';'); ++p) {
    unsigned digit;
    if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
    else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a') + 10;
    else if (*p >= 'A' && *p <= 'F') digit = (unsigned)(*p - 'A') + 10;
    else return false;
    if (digit >= base || *result > (UINT32_MAX - digit) / base) return false;
    *result = *result * base + digit;
  }
  return p != first;
}

static void line(z80_http_stream_t *s) {
  char *p = s->line;
  if (s->state == STATUS) {
    size_t n = strlen(p);
    if (n < 12 || (memcmp(p, "HTTP/1.0 ", 9) && memcmp(p, "HTTP/1.1 ", 9)) ||
        p[9] < '1' || p[9] > '5' || p[10] < '0' || p[10] > '9' ||
        p[11] < '0' || p[11] > '9' || (n > 12 && p[12] != ' ')) {
      s->error = "Invalid HTTP status line";
      return;
    }
    s->status = (unsigned)(p[9] - '0') * 100 + (unsigned)(p[10] - '0') * 10 + (unsigned)(p[11] - '0');
    s->state = HEADERS;
  } else if (s->state == CHUNK_SIZE) {
    if (!number(p, 16, &s->remaining)) s->error = "Invalid chunk size";
    else s->state = s->remaining ? CHUNK_BODY : TRAILERS;
  } else if (!*p) {
    if (s->state == TRAILERS) { s->done = true; return; }
    if (s->have_length && s->chunked) { s->error = "Ambiguous HTTP framing"; return; }
    if (s->status >= 100 && s->status < 200 && s->status != 101) {
      s->state = STATUS;
      s->have_length = s->chunked = false;
      s->remaining = 0;
      s->location[0] = 0;
      return;
    }
    if (s->status == 301 || s->status == 302 || s->status == 303 ||
        s->status == 307 || s->status == 308) {
      if (!s->location[0]) s->error = "Redirect without Location";
      else s->done = true;
      return;
    }
    if (s->status != 200 && s->status != 204) { s->error = "HTTP server returned an error"; return; }
    s->state = s->chunked ? CHUNK_SIZE : s->have_length ? BODY_LENGTH : BODY_CLOSE;
    s->done = s->status == 204 || (s->have_length && !s->remaining);
  } else {
    char *colon = strchr(p, ':');
    if (!colon || colon == p || *p == ' ' || *p == '\t') { s->error = "Invalid HTTP header"; return; }
    for (const char *name = p; name < colon; ++name) {
      unsigned char c = (unsigned char)*name;
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c))) {
        s->error = "Invalid HTTP header name";
        return;
      }
    }
    if (s->state == TRAILERS) return;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t') ++colon;
    char *end = colon + strlen(colon);
    while (end > colon && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    if (!strcasecmp(p, "Content-Length")) {
      if (s->have_length || !number(colon, 10, &s->remaining)) s->error = "Invalid Content-Length";
      s->have_length = true;
    } else if (!strcasecmp(p, "Transfer-Encoding")) {
      if (s->chunked || strcasecmp(colon, "chunked")) s->error = "Unsupported Transfer-Encoding";
      s->chunked = true;
    } else if (!strcasecmp(p, "Content-Encoding")) {
      if (strcasecmp(colon, "identity")) s->error = "Compressed files are not supported";
    } else if (!strcasecmp(p, "Location")) {
      if (s->location[0] || strlen(colon) >= sizeof(s->location)) s->error = "Invalid redirect Location";
      else strcpy(s->location, colon);
    }
  }
}

size_t z80_http_stream_feed(z80_http_stream_t *s, const uint8_t *data,
                            size_t length, bool (*emit)(uint8_t)) {
  size_t i = 0;
  while (i < length && !s->error && !s->done) {
    uint8_t c = data[i];
    if (s->state == BODY_LENGTH || s->state == BODY_CLOSE || s->state == CHUNK_BODY) {
      if (!emit(c)) break;
      ++i;
      if (s->state != BODY_CLOSE && --s->remaining == 0) {
        if (s->state == BODY_LENGTH) s->done = true;
        else s->state = CHUNK_CR;
      }
    } else if (s->state == CHUNK_CR || s->state == CHUNK_LF) {
      ++i;
      if (c != (s->state == CHUNK_CR ? '\r' : '\n')) s->error = "Missing chunk delimiter";
      else s->state = s->state == CHUNK_CR ? CHUNK_LF : CHUNK_SIZE;
    } else {
      ++i;
      if (++s->header_bytes > 8192) { s->error = "HTTP metadata too large"; break; }
      if (c == '\n') {
        if (!s->line_length || s->line[s->line_length - 1] != '\r') {
          s->error = "Missing HTTP CRLF";
          break;
        }
        s->line[--s->line_length] = 0;
        if (memchr(s->line, '\r', s->line_length)) {
          s->error = "Unexpected HTTP carriage return";
          break;
        }
        line(s);
        s->line_length = 0;
        /* Bound headers/trailers, not the total metadata of a large chunked file. */
        if (s->state == CHUNK_BODY) s->header_bytes = 0;
      } else if (!c || c == 127 || (c < 32 && c != '\r' && c != '\t') ||
                 s->line_length == sizeof(s->line) - 1) s->error = "Invalid or oversized HTTP line";
      else s->line[s->line_length++] = (char)c;
    }
  }
  return i;
}

void z80_http_stream_close(z80_http_stream_t *s) {
  if (!s->done && !s->error) {
    if (s->state == BODY_CLOSE) s->done = true;
    else s->error = "Truncated HTTP response";
  }
}
