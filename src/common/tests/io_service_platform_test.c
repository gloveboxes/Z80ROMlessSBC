#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../io_service_platform.c"

static uint64_t now;
static bool locked;
static const char *configured_timezone;

uint64_t time_us_64(void) { return now; }
uint32_t get_rand_32(void) { return 0xfedcba98u; }
uint32_t save_and_disable_interrupts(void) {
  assert(!locked);
  locked = true;
  return 7;
}
void restore_interrupts(uint32_t state) {
  assert(locked && state == 7);
  locked = false;
}
void pico_get_unique_board_id_string(char *text, size_t capacity) {
  assert(capacity >= 17);
  memcpy(text, "0123456789ABCDEF", 17);
}
bool environment_io_get(const char *key, char *value, size_t capacity) {
  assert(strcmp(key, "TZ") == 0);
  if (configured_timezone == NULL)
    return false;
  assert(strlen(configured_timezone) < capacity);
  strcpy(value, configured_timezone);
  return true;
}

int main(void) {
  char text[256];
  assert(z80_service_millis() == 0);
  assert(z80_service_random() == 0xba98);
  assert(z80_service_info(0, text, sizeof(text)) == 14);
  assert(strcmp(text, "z80romless-sbc") == 0);
  assert(z80_service_info(1, text, sizeof(text)) == 0);
  z80_io_ports_set_network_address("192.0.2.42");
  assert(z80_service_info(1, text, sizeof(text)) == 10);
  assert(strcmp(text, "192.0.2.42") == 0);
  assert(z80_service_info(2, text, sizeof(text)) == 16);
  assert(strcmp(text, "0123456789ABCDEF") == 0);
  assert(!z80_service_clock(1, text, sizeof(text)));
  now = 0x100000000ull * 1000; /* beyond the 32-bit millisecond rollover */
  z80_io_ports_set_unix_time(1704067200);
  now += 2000000;
  assert(z80_service_clock(1, text, sizeof(text)));
  assert(strcmp(text, "2024-01-01T00:00:02Z") == 0);
  configured_timezone = "AEST-10";
  assert(z80_service_clock(2, text, sizeof(text)));
  assert(strcmp(text, "2024-01-01T10:00:02") == 0);
  assert(z80_service_clock(3, text, sizeof(text)));
  assert(strcmp(text, "Monday, 01 January 2024") == 0);
  assert(z80_service_clock(4, text, sizeof(text)));
  assert(strcmp(text, "Mon 01/01/2024 10:00:02") == 0);
  z80_io_ports_set_network_address("");
  assert(z80_service_info(1, text, sizeof(text)) == 0);
  assert(!locked);
  puts("Clock synchronization, timezone, uptime and device info passed");
}
