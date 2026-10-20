#include "io_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "hardware/sync.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

static uint32_t unix_seconds;
static uint64_t sync_millis;
static char network_address[16];

void z80_io_ports_set_unix_time(uint32_t seconds) {
  if (seconds < 1704067200u)
    return;
  uint32_t saved = save_and_disable_interrupts();
  sync_millis = z80_service_millis();
  unix_seconds = seconds;
  restore_interrupts(saved);
}

void z80_io_ports_set_network_address(const char *address) {
  snprintf(network_address, sizeof(network_address), "%s", address);
}

uint64_t z80_service_millis(void) {
  return time_us_64() / 1000;
}

uint16_t z80_service_random(void) {
  return (uint16_t)get_rand_32();
}

size_t z80_service_info(uint8_t selector, char *text, size_t capacity) {
  if (selector == 1) {
    int length = snprintf(text, capacity, "%s", network_address);
    return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
  }
  if (selector == 2) {
    if (capacity < 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1)
      return 0;
    pico_get_unique_board_id_string(text, capacity);
    return strlen(text);
  }
  int length = snprintf(text, capacity, "z80romless-sbc");
  return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}

bool z80_service_clock(uint8_t operation, char *text, size_t capacity) {
  uint32_t saved = save_and_disable_interrupts();
  uint32_t seconds = unix_seconds;
  uint64_t base = sync_millis;
  restore_interrupts(saved);
  if (seconds == 0)
    return false;
  time_t now = (time_t)((uint64_t)seconds +
                        (z80_service_millis() - base) / 1000);
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
