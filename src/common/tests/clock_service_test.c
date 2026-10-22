#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../io_service.h"
#include "../../stage10_websocket_terminal/lwipopts.h"
#include "hardware/flash.h"
#include "pico/unique_id.h"
#include "z80sbc/flash_layout.h"

uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
static uint64_t now_us;
static bool interrupts_disabled;
static uint8_t response[IO_SERVICE_MAX_PAYLOAD + 1];
static size_t response_length;
static unsigned flash_writes;

uint64_t time_us_64(void) { return now_us; }
uint32_t get_rand_32(void) { return 0x1234; }
uint32_t save_and_disable_interrupts(void) {
  assert(!interrupts_disabled);
  interrupts_disabled = true;
  return 1;
}
void restore_interrupts(uint32_t state) {
  assert(interrupts_disabled && state == 1);
  interrupts_disabled = false;
}
void pico_get_unique_board_id_string(char *text, size_t capacity) {
  assert(capacity >= 17);
  memcpy(text, "0123456789ABCDEF", 17);
}
io_service_error_t z80_service_led(bool write, bool *state) {
  (void)write;
  (void)state;
  return IO_SERVICE_UNAVAILABLE;
}
bool z80_flash_backend_flush(void) { return true; }
void z80_flash_core1_service(void) {}
bool z80_flash_disk_quiescent(void) { return true; }
void z80_flash_disk_set_write_enabled(bool enabled) { (void)enabled; }
bool z80_flash_backend_env_write(unsigned slot, const uint8_t *sector) {
  assert(slot < 2);
  memcpy(test_flash + Z80_FLASH_ENV_OFFSET + slot * FLASH_SECTOR_SIZE,
         sector, FLASH_SECTOR_SIZE);
  ++flash_writes;
  return true;
}

static void begin(uint8_t group, const uint8_t *request, size_t length) {
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  z80_io_port_out(2, group);
  assert(z80_io_port_in(2) == IO_SERVICE_COLLECTING);
  z80_io_port_out(3, (uint8_t)length);
  z80_io_port_out(3, (uint8_t)(length >> 8));
  for (size_t i = 0; i < length; ++i)
    z80_io_port_out(3, request[i]);
}

static uint8_t finish(void) {
  z80_io_ports_core1_service();
  uint8_t status = z80_io_port_in(2);
  assert(status == IO_SERVICE_COMPLETE ||
         status == IO_SERVICE_RESPONSE_READY || status == IO_SERVICE_ERROR);
  response_length = 0;
  if (status == IO_SERVICE_COMPLETE) {
    z80_io_port_out(2, IO_SERVICE_ACK);
  } else {
    response_length = z80_io_port_in(4);
    response_length |= (size_t)z80_io_port_in(4) << 8;
    assert(response_length <= IO_SERVICE_MAX_PAYLOAD);
    for (size_t i = 0; i < response_length; ++i)
      response[i] = z80_io_port_in(4);
    response[response_length] = 0;
    if (status == IO_SERVICE_ERROR)
      z80_io_port_out(2, IO_SERVICE_ACK);
  }
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  return status;
}

static void expect_clock(uint8_t operation, const char *expected) {
  begin(IO_SERVICE_CLOCK, &operation, 1);
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY);
  assert(finish() == IO_SERVICE_RESPONSE_READY);
  assert(response_length == strlen(expected));
  assert(memcmp(response, expected, response_length) == 0);
}

static void expect_error(const uint8_t *request, size_t length,
                         io_service_error_t error) {
  begin(IO_SERVICE_CLOCK, request, length);
  assert(finish() == IO_SERVICE_ERROR);
  assert(response_length == 1 && response[0] == error);
}

static void set_timezone(const char *timezone) {
  uint8_t request[6 + 255] = {3, 2, 'T', 'Z'};
  size_t length = strlen(timezone);
  assert(length <= 255);
  request[4] = (uint8_t)length;
  memcpy(request + 6, timezone, length);
  begin(IO_SERVICE_ENVIRONMENT, request, 6 + length);
  assert(finish() == IO_SERVICE_COMPLETE);
}

static void start_clock_timer(void) {
  z80_io_port_out(23, 0x12);
  z80_io_port_out(23, 50);
  z80_io_port_out(23, 0);
}

static uint8_t clock_timer_active(void) {
  z80_io_port_out(23, 0x22);
  return z80_io_port_in(23);
}

int main(void) {
  memset(test_flash, 0xff, sizeof(test_flash));
  z80_io_ports_init();
  expect_clock(0, "0");
  SNTP_SET_SYSTEM_TIME(0);
  for (uint8_t operation = 1; operation <= 4; ++operation)
    expect_error(&operation, 1, IO_SERVICE_CLOCK_UNSYNCED);

  now_us = 123456789;
  expect_clock(0, "123");
  SNTP_SET_SYSTEM_TIME(1704067200u);
  expect_clock(1, "2024-01-01T00:00:00Z");
  expect_clock(2, "2024-01-01T00:00:00");
  expect_clock(3, "Monday, 01 January 2024");
  expect_clock(4, "Mon 01/01/2024 00:00:00");
  set_timezone("AEST-10");
  now_us += 2000000;
  expect_clock(1, "2024-01-01T00:00:02Z");
  expect_clock(2, "2024-01-01T10:00:02");
  expect_clock(3, "Monday, 01 January 2024");
  expect_clock(4, "Mon 01/01/2024 10:00:02");
  set_timezone("ACST-9:30");
  expect_clock(2, "2024-01-01T09:30:02");
  set_timezone("EST5");
  expect_clock(2, "2023-12-31T19:00:02");
  expect_clock(3, "Sunday, 31 December 2023");

  set_timezone("EST5EDT,M3.2.0/2,M11.1.0/2");
  SNTP_SET_SYSTEM_TIME(1710053999u);
  expect_clock(2, "2024-03-10T01:59:59");
  now_us += 1000000;
  expect_clock(2, "2024-03-10T03:00:00");
  expect_clock(1, "2024-03-10T07:00:00Z");

  const uint8_t remove_timezone[] = {4, 2, 'T', 'Z'};
  begin(IO_SERVICE_ENVIRONMENT, remove_timezone, sizeof(remove_timezone));
  assert(finish() == IO_SERVICE_COMPLETE);
  expect_clock(2, "2024-03-10T07:00:00");
  unsigned before = flash_writes;
  SNTP_SET_SYSTEM_TIME(0);
  expect_clock(1, "2024-03-10T07:00:00Z");

  start_clock_timer();
  assert(clock_timer_active() == 1);
  uint8_t operation = 2;
  begin(IO_SERVICE_CLOCK, &operation, 1);
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY);
  now_us += 49000;
  assert(clock_timer_active() == 1);
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY);
  now_us += 1000;
  assert(clock_timer_active() == 0);
  assert(finish() == IO_SERVICE_RESPONSE_READY);
  assert(response_length == 19);
  assert(strcmp((char *)response, "2024-03-10T07:00:00") == 0);

  now_us = (0x100000000ull - 25) * 1000;
  SNTP_SET_SYSTEM_TIME(1704067200u);
  start_clock_timer();
  now_us += 49000;
  assert(clock_timer_active() == 1);
  now_us += 1000;
  assert(clock_timer_active() == 0);
  expect_clock(0, "4294967");
  now_us += 1950000;
  expect_clock(1, "2024-01-01T00:00:02Z");

  const uint8_t invalid_operation[] = {5};
  expect_error(invalid_operation, 1, IO_SERVICE_UNKNOWN_OPERATION);
  const uint8_t invalid_length[] = {2, 0};
  expect_error(invalid_length, sizeof(invalid_length), IO_SERVICE_INVALID_LENGTH);
  assert(flash_writes == before);
  assert(!interrupts_disabled);
  puts("PASS: CLOCK time/date/uptime ABI, SNTP hook, persistent TZ and 50 ms timer");
}
