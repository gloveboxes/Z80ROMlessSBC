#include "io_service.h"
#include "z80sbc/network_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "hardware/sync.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/mutex.h"
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

static uint32_t unix_seconds;
static uint64_t sync_millis;
static char network_address[16];
auto_init_mutex(network_address_mutex);
static char cached_hostname[sizeof("zeta-80-000000")];
static bool hostname_initialized;
auto_init_mutex(hostname_mutex);

static size_t get_cached_hostname(char *text, size_t capacity) {
  if (capacity < sizeof(cached_hostname))
    return 0;
  mutex_enter_blocking(&hostname_mutex);
  if (!hostname_initialized) {
    char device_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(device_id, sizeof(device_id));
    size_t id_length = strlen(device_id);
    if (id_length >= 6) {
      char *suffix = device_id + id_length - 6;
      for (size_t i = 0; i < 6; ++i) {
        if (suffix[i] >= 'A' && suffix[i] <= 'F')
          suffix[i] = (char)(suffix[i] - 'A' + 'a');
      }
      int length = snprintf(cached_hostname, sizeof(cached_hostname),
                            "zeta-80-%s", suffix);
      hostname_initialized = length > 0 &&
                             (size_t)length < sizeof(cached_hostname);
    }
  }
  size_t length = 0;
  if (hostname_initialized) {
    memcpy(text, cached_hostname, sizeof(cached_hostname));
    length = strlen(cached_hostname);
  }
  mutex_exit(&hostname_mutex);
  return length;
}

void z80_io_ports_set_unix_time(uint32_t seconds) {
  if (seconds < 1704067200u)
    return;
  uint32_t saved = save_and_disable_interrupts();
  sync_millis = z80_service_millis();
  unix_seconds = seconds;
  restore_interrupts(saved);
}

void z80_io_ports_set_network_address(const char *address) {
  mutex_enter_blocking(&network_address_mutex);
  snprintf(network_address, sizeof(network_address), "%s", address);
  mutex_exit(&network_address_mutex);
}

size_t z80_io_ports_get_network_address(char *address, size_t capacity) {
  mutex_enter_blocking(&network_address_mutex);
  int length = snprintf(address, capacity, "%s", network_address);
  mutex_exit(&network_address_mutex);
  return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}

uint64_t z80_service_millis(void) {
  return time_us_64() / 1000;
}

uint16_t z80_service_random(void) {
  return (uint16_t)get_rand_32();
}

size_t z80_service_info(uint8_t selector, char *text, size_t capacity) {
  if (selector == 1) {
    return z80_io_ports_get_network_address(text, capacity);
  }
  if (selector == 2) {
    if (capacity < 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1)
      return 0;
    pico_get_unique_board_id_string(text, capacity);
    return strlen(text);
  }
  return get_cached_hostname(text, capacity);
}

time_t z80_network_time(time_t *result) {
  uint32_t saved = save_and_disable_interrupts();
  uint32_t seconds = unix_seconds;
  uint64_t base = sync_millis;
  restore_interrupts(saved);
  time_t now = seconds == 0 ? 0 : (time_t)((uint64_t)seconds +
                                          (z80_service_millis() - base) / 1000);
  if (result != NULL)
    *result = now;
  return now;
}

bool z80_service_clock(uint8_t operation, char *text, size_t capacity) {
  time_t now = z80_network_time(NULL);
  if (now == 0)
    return false;
  char timezone[256];
  if (!environment_io_get("TZ", timezone, sizeof(timezone)))
    memcpy(timezone, "UTC0", 5);
  if (setenv("TZ", timezone, 1) != 0)
    return false;
  tzset();
  struct tm *calendar = operation == 1 ? gmtime(&now) : localtime(&now);
  if (calendar == NULL)
    return false;
  static const char *formats[] = {
    "", "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%dT%H:%M:%S",
    "%A, %d %B %Y", "%a %m/%d/%Y %H:%M:%S",
  };
  return strftime(text, capacity, formats[operation], calendar) != 0;
}
