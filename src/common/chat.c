#include "chat.h"
#include "chat_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

static bool (*start_request)(const z80_chat_endpoint_t *, const char *, size_t);
static void (*cancel_request)(void);
static z80_chat_endpoint_t endpoint;
static z80_chat_stream_t stream;
static char request[Z80_CHAT_REQUEST_MAX];
static uint8_t response[Z80_CHAT_RESPONSE_BYTES];
static size_t request_length, head, tail, count, error_position;
static bool active, online;
static uint32_t network_generation;
static char error_text[160];

static void clear_response(void) {
  head = tail = count = error_position = 0;
  error_text[0] = '\0';
}

void z80_chat_error(const char *message) {
  if (!error_text[0]) {
    snprintf(error_text, sizeof(error_text), "\nChat: %s\n", message);
    printf("[chat] %s\n", message);
  }
  active = false;
}

static bool emit(uint8_t byte) {
  if (count == sizeof(response))
    return false;
  response[head] = byte;
  head = (head + 1) % sizeof(response);
  ++count;
  return true;
}

bool z80_chat_active(void) { return active; }

size_t z80_chat_receive(const uint8_t *data, size_t length) {
  if (!active)
    return 0;
  size_t consumed = z80_chat_stream_feed(&stream, data, length, emit);
  if (stream.error)
    z80_chat_error(stream.error);
  else if (stream.done)
    active = false;
  return consumed;
}

void z80_chat_network_poll(bool connected, uint32_t generation) {
  if (active && (!connected || generation != network_generation)) {
    cancel_request();
    z80_chat_error("Network disconnected or reconfigured");
  }
  online = connected;
  network_generation = generation;
  z80_chat_receive(NULL, 0);
}

void z80_chat_init(bool (*start)(const z80_chat_endpoint_t *, const char *, size_t),
                   void (*cancel)(void)) {
  start_request = start;
  cancel_request = cancel;
}

static bool setting(const char *name, char *value, size_t capacity) {
  char text[256];
  if (!environment_io_get(name, text, sizeof(text))) {
    value[0] = '\0';
    return true;
  }
  if (strlen(text) >= capacity) {
    z80_chat_error("CHAT setting exceeds its size limit");
    return false;
  }
  strcpy(value, text);
  return true;
}

static bool parse_endpoint(const char *url) {
  const char *host;
  if (strncmp(url, "https://", 8) == 0) {
    endpoint.https = true;
    endpoint.port = 443;
    host = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    endpoint.https = false;
    endpoint.port = 80;
    host = url + 7;
  } else {
    return false;
  }
  const char *path = strchr(host, '/');
  if (!path || path == host || (size_t)(path - host) >= sizeof(endpoint.authority) ||
      strlen(path) >= sizeof(endpoint.path))
    return false;
  for (const char *p = host; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 ||
        *p == '@' || *p == '#' || *p == '\\')
      return false;
  memcpy(endpoint.authority, host, (size_t)(path - host));
  endpoint.authority[path - host] = '\0';
  strcpy(endpoint.path, path);
  const char *colon = memchr(host, ':', (size_t)(path - host));
  size_t host_length = (size_t)((colon ? colon : path) - host);
  if (!host_length || host_length >= sizeof(endpoint.host))
    return false;
  for (size_t i = 0; i < host_length; ++i) {
    char c = host[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-'))
      return false;
  }
  memcpy(endpoint.host, host, host_length);
  endpoint.host[host_length] = '\0';
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
    endpoint.port = (uint16_t)port;
  }
  return true;
}

static bool load_settings(void) {
  char provider[16], url[256];
  memset(&endpoint, 0, sizeof(endpoint));
  environment_io_close();
  environment_io_init(NULL);
  if (environment_input(ENVIRONMENT_PORT_COMMAND) != 0) {
    z80_chat_error("Cannot read CHAT ENV settings");
    return false;
  }
  if (!setting("CHAT_PROVIDER", provider, sizeof(provider)))
    return false;
  bool openai = !provider[0] || strcmp(provider, "openai") == 0;
  if (openai) {
    strcpy(url, "https://api.openai.com/v1/chat/completions");
    if (!setting("CHAT_OPENAI_KEY", endpoint.key, sizeof(endpoint.key)))
      return false;
    if (!endpoint.key[0]) {
      z80_chat_error("Set CHAT_OPENAI_KEY in ENV");
      return false;
    }
    for (const char *p = endpoint.key; *p; ++p)
      if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) {
        z80_chat_error("Invalid CHAT_OPENAI_KEY");
        return false;
      }
  } else if (strcmp(provider, "compatible") == 0 ||
             strcmp(provider, "ollama") == 0) {
    if (!setting("CHAT_ENDPOINT", url, sizeof(url)))
      return false;
    /* Never forward a saved OpenAI credential to a different provider. */
  } else {
    z80_chat_error("CHAT_PROVIDER must be openai, compatible or ollama");
    return false;
  }
  if (!parse_endpoint(url)) {
    z80_chat_error("Invalid CHAT_ENDPOINT; use a full http(s) IPv4/DNS URL");
    return false;
  }
  return true;
}

static uint8_t status(void) {
  if (count || error_text[error_position])
    return Z80_CHAT_DATA_READY;
  return active ? Z80_CHAT_WAITING : Z80_CHAT_EOF;
}

io_service_error_t z80_chat_service(const uint8_t *data, size_t length,
                                   io_service_result_t *result) {
  if (!start_request)
    return IO_SERVICE_UNAVAILABLE;
  if (!length)
    return IO_SERVICE_INVALID_LENGTH;
  uint8_t operation = data[0];
  if (operation > 5)
    return IO_SERVICE_UNKNOWN_OPERATION;
  if ((operation == 1 && length < 2) ||
      (operation == 5 ? length != 3 : operation != 1 && length != 1))
    return IO_SERVICE_INVALID_LENGTH;
  if (operation == 0) {
    /* CHATC11 resets the request before resetting the response, including
     * after its local buffer fills or its idle timeout expires. */
    cancel_request();
    active = false;
    request_length = 0;
    request[0] = '\0';
  } else if (operation == 1) {
    if (active)
      return IO_SERVICE_BUSY_ERROR;
    if (length - 1 > sizeof(request) - 1 - request_length)
      return IO_SERVICE_CAPACITY;
    if (memchr(data + 1, 0, length - 1))
      return IO_SERVICE_INVALID_PARAMETER;
    memcpy(request + request_length, data + 1, length - 1);
    request_length += length - 1;
    request[request_length] = '\0';
  } else if (operation == 2) {
    cancel_request();
    active = false;
    clear_response();
  } else if (operation == 3) {
    if (active)
      return IO_SERVICE_BUSY_ERROR;
    if (!request_length)
      return IO_SERVICE_INVALID_STATE;
    clear_response();
    z80_chat_stream_init(&stream);
    if (!online)
      z80_chat_error("Wi-Fi unavailable");
    else if (load_settings()) {
      active = true;
      if (!start_request(&endpoint, request, request_length))
        z80_chat_error("Cannot start HTTP request");
    }
  } else {
    result->has_response = true;
    if (operation == 4) {
      result->data[0] = status();
      result->length = 1;
    } else {
      size_t maximum = data[1] | ((size_t)data[2] << 8);
      if (!maximum || maximum > IO_SERVICE_MAX_PAYLOAD - 3)
        return IO_SERVICE_INVALID_PARAMETER;
      size_t used = 0;
      while (used < maximum && count) {
        result->data[3 + used++] = response[tail];
        tail = (tail + 1) % sizeof(response);
        --count;
      }
      while (used < maximum && error_text[error_position])
        result->data[3 + used++] = (uint8_t)error_text[error_position++];
      result->data[0] = status();
      result->data[1] = (uint8_t)used;
      result->data[2] = (uint8_t)(used >> 8);
      result->length = used + 3;
    }
  }
  return IO_SERVICE_OK;
}
