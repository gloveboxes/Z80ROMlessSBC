#include "http_io.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

/* A single mailbox has exclusive ownership: core 0 writes IDLE, core 1 writes
 * PENDING, core 0 reads DONE. Release/acquire publishes metadata and bytes.
 * Cancellation changes the session, never a buffer still owned by core 1. */
enum { SLOT_IDLE, SLOT_PENDING, SLOT_DONE };
static uint32_t slot_state, session;
static struct {
  uint32_t session;
  char filename[Z80_HTTP_URL_MAX];
  size_t length;
  uint8_t status, data[256];
} slot;

static struct {
  char filename[Z80_HTTP_URL_MAX];
  size_t name_length, length, position;
  uint8_t status, final_status;
  bool collecting, overflow, want_chunk;
} port;

/* Core 1 owns everything below. The ring applies TCP backpressure without
 * buffering a whole file or blocking disk/USB/network service. */
static bool (*start_request)(const z80_http_url_t *);
static void (*cancel_request)(void);
static uint32_t worker_session, network_generation, transfer_network;
static bool online, opened, active, backend_running;
static unsigned redirects;
static uint8_t worker_error_status;
static z80_http_url_t target;
static z80_http_stream_t stream;
static uint8_t ring[1024];
static size_t head, tail, count;

static void cancel_backend(void) {
  if (backend_running && cancel_request) cancel_request();
  backend_running = false;
}

void z80_http_io_init(void) {
  memset(&port, 0, sizeof(port));
  __atomic_add_fetch(&session, 1, __ATOMIC_RELEASE);
}

static void pump(void) {
  uint32_t state = __atomic_load_n(&slot_state, __ATOMIC_ACQUIRE);
  if (state == SLOT_DONE) {
    if (slot.session == __atomic_load_n(&session, __ATOMIC_ACQUIRE) && port.want_chunk) {
      port.want_chunk = false;
      port.length = slot.length ? slot.length + 1 : 0;
      port.position = 0;
      port.final_status = slot.status;
      port.status = slot.length ? HTTP_DATA_READY : slot.status;
      /* Leave DONE owned by core 0 until all bytes have been read. */
      if (port.length) return;
    }
    __atomic_store_n(&slot_state, SLOT_IDLE, __ATOMIC_RELEASE);
    state = SLOT_IDLE;
  }
  if (state == SLOT_IDLE && port.want_chunk) {
    slot.session = __atomic_load_n(&session, __ATOMIC_ACQUIRE);
    memcpy(slot.filename, port.filename, port.name_length + 1);
    __atomic_store_n(&slot_state, SLOT_PENDING, __ATOMIC_RELEASE);
  }
}

void z80_http_io_out(uint8_t address, uint8_t data) {
  if (address == HTTP_COMMAND_PORT) {
    if (data == HTTP_SET_FILENAME || data == HTTP_CLOSE) {
      __atomic_add_fetch(&session, 1, __ATOMIC_RELEASE);
      memset(&port, 0, sizeof(port));
      port.collecting = data == HTTP_SET_FILENAME;
      pump();
    } else if (data == HTTP_REQUEST_CHUNK) {
      if (port.want_chunk || port.position < port.length) return;
      if (port.collecting || port.overflow || !port.name_length || port.status == HTTP_ERROR) {
        port.status = HTTP_ERROR;
        return;
      }
      if (port.status == HTTP_EOF) return;
      port.want_chunk = true;
      port.status = HTTP_BUSY;
      pump();
    }
  } else if (address == HTTP_DATA_PORT && port.collecting) {
    if (!data) {
      port.collecting = false;
      port.filename[port.name_length] = 0;
      if (port.overflow || !port.name_length) port.status = HTTP_ERROR;
    } else if (port.name_length < sizeof(port.filename) - 1 && !port.overflow) {
      port.filename[port.name_length++] = (char)data;
    } else {
      port.overflow = true;
      port.status = HTTP_ERROR;
    }
  }
}

uint8_t z80_http_io_in(uint8_t address) {
  /* Do not recycle DONE while core 0 is still reading its data. */
  if (port.position >= port.length) pump();
  if (address == HTTP_COMMAND_PORT) return port.status;
  if (address != HTTP_DATA_PORT || port.position >= port.length) return 0;
  size_t position = port.position++;
  uint8_t byte = position ? slot.data[position - 1] : (uint8_t)slot.length;
  if (port.position == port.length) {
    port.status = port.final_status;
    __atomic_store_n(&slot_state, SLOT_IDLE, __ATOMIC_RELEASE);
  }
  return byte;
}

void z80_http_io_backend(bool (*start)(const z80_http_url_t *), void (*cancel)(void)) {
  start_request = start;
  cancel_request = cancel;
}

void z80_http_io_network_state(bool connected, uint32_t generation) {
  online = connected;
  network_generation = generation;
}

bool z80_http_io_active(void) { return active; }

void z80_http_io_error(const char *message) {
  if (!stream.error || active) printf("[http] %s\n", message);
  if (!stream.error) {
    stream.error = message;
  }
  active = false;
}

static bool emit(uint8_t byte) {
  if (count == sizeof(ring)) return false;
  ring[head] = byte;
  head = (head + 1) % sizeof(ring);
  ++count;
  return true;
}

size_t z80_http_io_receive(const uint8_t *data, size_t length) {
  if (!active) return 0;
  size_t used = z80_http_stream_feed(&stream, data, length, emit);
  if (stream.error) z80_http_io_error(stream.error);
  return used;
}

void z80_http_io_remote_close(void) {
  z80_http_stream_close(&stream);
  if (stream.error) z80_http_io_error(stream.error);
}

typedef enum {
  HTTP_TARGET_OK,
  HTTP_TARGET_INVALID,
  HTTP_TARGET_MISSING_BASE_URL,
} http_target_result_t;

static http_target_result_t load_target(const char *name) {
  if (!strncasecmp(name, "http://", 7) || !strncasecmp(name, "https://", 8))
    return z80_http_url_parse(name, &target) ? HTTP_TARGET_OK : HTTP_TARGET_INVALID;
  char base[256];
  environment_io_close();
  environment_io_init(NULL);
  if (environment_input(ENVIRONMENT_PORT_COMMAND)) return HTTP_TARGET_INVALID;
  if (!environment_io_get("HTTP_BASE_URL", base, sizeof(base)) || !base[0])
    return HTTP_TARGET_MISSING_BASE_URL;
  if (!z80_http_url_parse(base, &target) || strchr(target.path, '?'))
    return HTTP_TARGET_INVALID;
  /* Relative names extend the base directory; a leading slash uses the
   * origin root. Validate the result without a second large URL object. */
  for (const char *p = name; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 ||
        *p == '#' || *p == '\\' || *p == '@' || *p == ':') return HTTP_TARGET_INVALID;
  size_t prefix = *name == '/' ? 0 : strlen(target.path);
  bool separator = prefix && target.path[prefix - 1] != '/';
  int used = snprintf(target.path + prefix, sizeof(target.path) - prefix,
                       "%s%s", separator ? "/" : "", name);
  return used > 0 && (size_t)used < sizeof(target.path) - prefix
             ? HTTP_TARGET_OK
             : HTTP_TARGET_INVALID;
}

void z80_http_io_core1_service(void) {
  uint32_t current = __atomic_load_n(&session, __ATOMIC_ACQUIRE);
  if (current != worker_session) {
    cancel_backend();
    worker_session = current;
    opened = active = false;
    head = tail = count = redirects = 0;
    worker_error_status = HTTP_ERROR;
    z80_http_stream_init(&stream);
  }
  if (active && (!online || transfer_network != network_generation))
    z80_http_io_error("Network disconnected or reconfigured");
  if (active && stream.done) {
    cancel_backend();
    if (stream.status >= 300) {
      if (++redirects > 5 || !z80_http_url_redirect(&target, stream.location)) {
        z80_http_io_error("Invalid redirect, HTTPS downgrade or redirect limit exceeded");
      } else {
        z80_http_stream_init(&stream);
        backend_running = true;
        if (!start_request(&target)) z80_http_io_error("Cannot start HTTP redirect");
      }
    } else active = false;
  }
  if (!active && stream.error) cancel_backend();
  if (__atomic_load_n(&slot_state, __ATOMIC_ACQUIRE) != SLOT_PENDING) return;
  if (slot.session != current) {
    slot.length = 0;
    slot.status = HTTP_IDLE;
  } else {
    if (!opened) {
      opened = true;
      transfer_network = network_generation;
      if (!start_request || !online) z80_http_io_error("HTTP network unavailable");
      else {
        http_target_result_t result = load_target(slot.filename);
        if (result == HTTP_TARGET_MISSING_BASE_URL) {
          worker_error_status = HTTP_MISSING_BASE_URL;
          z80_http_io_error("Missing or empty HTTP_BASE_URL; set it in ENV");
        } else if (result != HTTP_TARGET_OK) {
          z80_http_io_error("Invalid URL or HTTP_BASE_URL");
        } else {
          active = true;
          backend_running = true;
          if (!start_request(&target)) z80_http_io_error("Cannot start HTTP request");
        }
      }
    }
    if (stream.error) {
      count = 0;
      slot.length = 0;
      slot.status = worker_error_status;
    } else {
      if (count < sizeof(slot.data) && (active || !stream.done)) return;
      slot.length = count < sizeof(slot.data) ? count : sizeof(slot.data);
      for (size_t i = 0; i < slot.length; ++i) {
        slot.data[i] = ring[tail];
        tail = (tail + 1) % sizeof(ring);
      }
      count -= slot.length;
      slot.status = !active && stream.done && !count ? HTTP_EOF : HTTP_IDLE;
    }
  }
  __atomic_store_n(&slot_state, SLOT_DONE, __ATOMIC_RELEASE);
}
