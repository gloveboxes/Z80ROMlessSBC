#ifndef Z80SBC_CHAT_ENDPOINT_H
#define Z80SBC_CHAT_ENDPOINT_H

#include <string.h>
#include "chat.h"

static inline bool z80_chat_parse_endpoint(const char *url,
                                           z80_chat_endpoint_t *endpoint) {
  const char *host;
  if (strncmp(url, "https://", 8) == 0) {
    endpoint->https = true;
    endpoint->port = 443;
    host = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    endpoint->https = false;
    endpoint->port = 80;
    host = url + 7;
  } else {
    return false;
  }
  const char *path = strchr(host, '/');
  if (!path || path == host ||
      (size_t)(path - host) >= sizeof(endpoint->authority) ||
      strlen(path) >= sizeof(endpoint->path))
    return false;
  for (const char *p = host; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '@' ||
        *p == '#' || *p == '\\')
      return false;
  memcpy(endpoint->authority, host, (size_t)(path - host));
  endpoint->authority[path - host] = '\0';
  strcpy(endpoint->path, path);
  const char *colon = memchr(host, ':', (size_t)(path - host));
  size_t host_length = (size_t)((colon ? colon : path) - host);
  if (!host_length || host_length >= sizeof(endpoint->host))
    return false;
  for (size_t i = 0; i < host_length; ++i) {
    char c = host[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-'))
      return false;
  }
  memcpy(endpoint->host, host, host_length);
  endpoint->host[host_length] = '\0';
  if (colon) {
    unsigned port = 0;
    if (colon + 1 == path)
      return false;
    for (const char *p = colon + 1; p < path; ++p) {
      if (*p < '0' || *p > '9' || port > 6553)
        return false;
      port = port * 10 + (unsigned)(*p - '0');
      if (port > 65535)
        return false;
    }
    if (!port)
      return false;
    endpoint->port = (uint16_t)port;
  }
  return true;
}

#endif
