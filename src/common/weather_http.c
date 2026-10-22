#include "weather_http.h"
#include "weather.h"

#include <stdint.h>
#include <string.h>
#include <strings.h>

static bool decimal(const char *first, const char *end, size_t *value) {
  *value = 0;
  if (first == end)
    return false;
  while (first < end) {
    if (*first < '0' || *first > '9' ||
        *value > (Z80_WEATHER_BODY_MAX - (unsigned)(*first - '0')) / 10)
      return false;
    *value = *value * 10 + (unsigned)(*first++ - '0');
  }
  return true;
}

bool z80_weather_http_decode(char *wire, size_t length, unsigned *status,
                             const char **body, size_t *body_length) {
  if (memchr(wire, 0, length))
    return false;
  char *header_end = strstr(wire, "\r\n\r\n");
  if (!header_end || header_end - wire > 2048)
    return false;
  char *line = strstr(wire, "\r\n");
  if (!line || line - wire < 12 ||
      (memcmp(wire, "HTTP/1.0 ", 9) && memcmp(wire, "HTTP/1.1 ", 9)) ||
      wire[9] < '1' || wire[9] > '5' ||
      wire[10] < '0' || wire[10] > '9' ||
      wire[11] < '0' || wire[11] > '9' ||
      (line - wire > 12 && wire[12] != ' '))
    return false;
  *status = (unsigned)(wire[9] - '0') * 100 +
            (unsigned)(wire[10] - '0') * 10 + (unsigned)(wire[11] - '0');
  bool chunked = false, have_length = false;
  size_t content_length = 0;
  for (line += 2; line < header_end; ) {
    char *end = strstr(line, "\r\n");
    if (!end || end > header_end)
      return false;
    char *colon = memchr(line, ':', (size_t)(end - line));
    if (!colon)
      return false;
    char *value = colon + 1, *value_end = end;
    while (value < value_end && (*value == ' ' || *value == '\t'))
      ++value;
    while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t'))
      --value_end;
    size_t name_length = (size_t)(colon - line);
    if (name_length == 14 && !strncasecmp(line, "Content-Length", 14)) {
      if (have_length || !decimal(value, value_end, &content_length))
        return false;
      have_length = true;
    } else if (name_length == 17 && !strncasecmp(line, "Transfer-Encoding", 17)) {
      if (chunked || value_end - value != 7 || strncasecmp(value, "chunked", 7))
        return false;
      chunked = true;
    } else if (name_length == 16 && !strncasecmp(line, "Content-Encoding", 16)) {
      if (value_end - value != 8 || strncasecmp(value, "identity", 8))
        return false;
    }
    line = end + 2;
  }
  char *data = header_end + 4, *end = wire + length;
  if (!chunked) {
    *body_length = (size_t)(end - data);
    if (*body_length > Z80_WEATHER_BODY_MAX ||
        (have_length && *body_length != content_length))
      return false;
  } else {
    if (have_length)
      return false;
    char *read = data, *write = data;
    for (;;) {
      char *next = strstr(read, "\r\n");
      if (!next || next - read > 64 || next == read)
        return false;
      size_t chunk = 0;
      char *digit = read;
      for (; digit < next && *digit != ';'; ++digit) {
        unsigned value;
        if (*digit >= '0' && *digit <= '9') value = (unsigned)(*digit - '0');
        else if (*digit >= 'a' && *digit <= 'f') value = (unsigned)(*digit - 'a') + 10;
        else if (*digit >= 'A' && *digit <= 'F') value = (unsigned)(*digit - 'A') + 10;
        else return false;
        if (chunk > (Z80_WEATHER_BODY_MAX - value) / 16)
          return false;
        chunk = chunk * 16 + value;
      }
      if (digit == read)
        return false;
      read = next + 2;
      if (chunk == 0) {
        for (;;) {
          next = strstr(read, "\r\n");
          if (!next)
            return false;
          if (next == read) {
            if (next + 2 != end)
              return false;
            break;
          }
          if (!memchr(read, ':', (size_t)(next - read)))
            return false;
          read = next + 2;
        }
        *body_length = (size_t)(write - data);
        break;
      }
      if ((size_t)(end - read) < chunk + 2 ||
          (size_t)(write - data) + chunk > Z80_WEATHER_BODY_MAX ||
          read[chunk] != '\r' || read[chunk + 1] != '\n')
        return false;
      memmove(write, read, chunk);
      write += chunk;
      read += chunk + 2;
    }
  }
  data[*body_length] = '\0';
  *body = data;
  return true;
}
