#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../io_service.h"
#include "../weather.h"
#include "../chat.h"
#include "../http_io.h"
#include "hardware/flash.h"
#include "z80sbc/environment.h"
#include "z80sbc/flash_layout.h"

uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
static uint64_t now;
static unsigned writes;
static int cut_after = -1;
static uint8_t reply[IO_SERVICE_MAX_PAYLOAD + 1];
static unsigned reply_length;
static bool flush_ok = true;
static bool disk_write_enabled = true;
static bool led_available = true;
static bool led_state;
static unsigned led_calls;

io_service_error_t z80_service_led(bool write, bool *state) {
  ++led_calls;
  if (!led_available)
    return IO_SERVICE_UNAVAILABLE;
  if (write)
    led_state = *state;
  else
    *state = led_state;
  return IO_SERVICE_OK;
}

uint64_t z80_service_millis(void) { return now; }
uint16_t z80_service_random(void) { return 0x1234; }
size_t z80_service_info(uint8_t selector, char *text, size_t capacity) {
  (void)selector;
  assert(capacity > 4);
  memcpy(text, "test", 5);
  return 4;
}
bool z80_service_clock(uint8_t operation, char *text, size_t capacity) {
  (void)operation; (void)text; (void)capacity;
  return false;
}
bool z80_flash_backend_flush(void) { return flush_ok; }
void z80_flash_core1_service(void) {}
bool z80_flash_disk_quiescent(void) { return true; }
void z80_flash_disk_set_write_enabled(bool enabled) {
  disk_write_enabled = enabled;
}
bool z80_flash_backend_env_write(unsigned slot, const uint8_t *sector) {
  assert(slot < 2);
  uint8_t *target = test_flash + Z80_FLASH_ENV_OFFSET + slot * FLASH_SECTOR_SIZE;
  ++writes;
  memset(target, 0xff, FLASH_SECTOR_SIZE);
  size_t bytes = cut_after < 0 ? FLASH_SECTOR_SIZE : (size_t)cut_after;
  size_t payload_bytes = FLASH_SECTOR_SIZE - FLASH_PAGE_SIZE;
  memcpy(target + FLASH_PAGE_SIZE, sector + FLASH_PAGE_SIZE,
         bytes < payload_bytes ? bytes : payload_bytes);
  if (bytes > payload_bytes)
    memcpy(target, sector, bytes - payload_bytes);
  return bytes == FLASH_SECTOR_SIZE;
}

static void reset(void) {
  z80_io_port_out(2, IO_SERVICE_RESET);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
}

static void send(uint8_t group, const uint8_t *data, unsigned length) {
  reset();
  z80_io_port_out(2, group);
  assert(z80_io_port_in(2) == IO_SERVICE_COLLECTING);
  z80_io_port_out(3, (uint8_t)length);
  z80_io_port_out(3, (uint8_t)(length >> 8));
  for (unsigned i = 0; i < length; ++i)
    z80_io_port_out(3, data[i]);
}

static uint8_t finish(void) {
  z80_io_ports_core1_service();
  uint8_t status = z80_io_port_in(2);
  reply_length = 0;
  if (status == IO_SERVICE_ERROR || status == IO_SERVICE_RESPONSE_READY) {
    unsigned low = z80_io_port_in(4);
    reply_length = low | ((unsigned)z80_io_port_in(4) << 8);
    assert(reply_length <= IO_SERVICE_MAX_PAYLOAD);
    for (unsigned i = 0; i < reply_length; ++i)
      reply[i] = z80_io_port_in(4);
    reply[reply_length] = 0;
    if (status == IO_SERVICE_RESPONSE_READY)
      assert(z80_io_port_in(2) == IO_SERVICE_READY);
  }
  return status;
}

static void call(uint8_t group, const uint8_t *data, unsigned length,
                 uint8_t status) {
  send(group, data, length);
  uint8_t actual = finish();
  if (actual != status)
    fprintf(stderr, "group=%02x operation=%u expected=%u actual=%u error=%u\n",
            group, length ? data[0] : 0, status, actual,
            reply_length ? reply[0] : 0);
  assert(actual == status);
}

static void error(uint8_t group, const uint8_t *data, unsigned length,
                  io_service_error_t expected) {
  call(group, data, length, IO_SERVICE_ERROR);
  assert(reply_length == 1 && reply[0] == expected);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
}

static void cli(const char *command, uint8_t status) {
  uint8_t request[512] = {9};
  size_t length = strlen(command);
  assert(length < sizeof(request));
  memcpy(request + 1, command, length);
  call(IO_SERVICE_ENVIRONMENT, request, (unsigned)length + 1, status);
}

static void test_transport(void) {
  for (unsigned port = 0; port < 256; ++port) {
    unsigned masked = port & 0x17;
    assert(z80_io_ports_handles((uint8_t)port) ==
           ((masked >= 2 && masked <= 4) || masked >= 0x15));
    if (masked == 2) {
      z80_io_port_out((uint8_t)port, IO_SERVICE_RESET);
      z80_io_port_out((uint8_t)port, IO_SERVICE_IDENTIFY);
      assert(finish() == IO_SERVICE_RESPONSE_READY);
      assert(strcmp((char *)reply, "ROMLESS-SVC/1") == 0);
    } else if (masked == 3) {
      reset();
      z80_io_port_out(2, IO_SERVICE_CLOCK);
      z80_io_port_out((uint8_t)port, 1);
      z80_io_port_out((uint8_t)port, 0);
      z80_io_port_out((uint8_t)port, 0);
      assert(finish() == IO_SERVICE_RESPONSE_READY);
      assert(reply_length == 1 && reply[0] == '0');
    } else if (masked == 4) {
      reset();
      z80_io_port_out(2, IO_SERVICE_IDENTIFY);
      assert(z80_io_port_in((uint8_t)port) == 13);
      assert(z80_io_port_in((uint8_t)port) == 0);
      for (unsigned i = 0; i < 13; ++i)
        assert(z80_io_port_in((uint8_t)port) == (uint8_t)"ROMLESS-SVC/1"[i]);
      assert(z80_io_port_in(2) == IO_SERVICE_READY);
    } else if (masked == HTTP_COMMAND_PORT || masked == HTTP_DATA_PORT) {
      uint8_t command_port = masked == HTTP_COMMAND_PORT ? (uint8_t)port : 21;
      uint8_t data_port = masked == HTTP_DATA_PORT ? (uint8_t)port : 22;
      z80_io_port_out(command_port, HTTP_SET_FILENAME);
      assert(z80_io_port_in(command_port) == HTTP_IDLE);
      for (const char *p = "http://example.org/FILE.BIN"; *p; ++p)
        z80_io_port_out(data_port, (uint8_t)*p);
      z80_io_port_out(data_port, 0);
      z80_io_port_out(command_port, HTTP_REQUEST_CHUNK);
      assert(z80_io_port_in(command_port) == HTTP_BUSY);
      z80_io_ports_core1_service();
      /* This common-service fixture has no installed network backend. */
      assert(z80_io_port_in(command_port) == HTTP_ERROR);
      z80_io_port_out(command_port, HTTP_CLOSE);
      assert(z80_io_port_in(command_port) == HTTP_IDLE);
      assert(z80_io_port_in(2) == IO_SERVICE_READY);
    }
  }
  reset();
  assert(z80_io_port_in(4) == 0);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_UNDERFLOW);
  reset();
  z80_io_port_out(2, 0xfe);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_UNKNOWN_GROUP);
  reset();
  z80_io_port_out(2, IO_SERVICE_CLOCK);
  z80_io_port_out(3, 0);
  z80_io_port_out(3, 0);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_INVALID_LENGTH);
  reset();
  z80_io_port_out(2, IO_SERVICE_CLOCK);
  z80_io_port_out(3, 1);
  z80_io_port_out(3, 8);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_INVALID_LENGTH);
  reset();
  z80_io_port_out(2, IO_SERVICE_IDENTIFY);
  z80_io_port_out(2, IO_SERVICE_IDENTIFY);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_BUSY_ERROR);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_ERROR);

  uint8_t operation[] = {0};
  send(IO_SERVICE_CLOCK, operation, 1);
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY);
  assert(!z80_io_ports_quiescent());
  reset();
  z80_io_ports_core1_service();
  assert(z80_io_port_in(2) == IO_SERVICE_READY); /* stale result discarded */
  call(IO_SERVICE_CLOCK, operation, 1, IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "0") == 0);

  send(IO_SERVICE_CLOCK, operation, 1);
  reset();
  send(IO_SERVICE_CLOCK, operation, 1);
  assert(finish() == IO_SERVICE_ERROR && reply[0] == IO_SERVICE_BUSY_ERROR);
  reset();
  uint8_t maximum[2048] = {0};
  error(IO_SERVICE_CLOCK, maximum, sizeof(maximum), IO_SERVICE_INVALID_LENGTH);
}

static void test_worker_response_lifetime(void) {
  const uint8_t version[] = {3};
  const char expected[] = "Pico 2 W Z80ROMlessSBC ROMLESS-SVC/1\n";
  send(IO_SERVICE_UTILITY, version, sizeof(version));
  z80_io_ports_core1_service();
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  assert(z80_io_port_in(4) == sizeof(expected) - 1);
  assert(z80_io_port_in(4) == 0);
  for (size_t i = 0; i < sizeof(expected) - 1; ++i) {
    z80_io_ports_core1_service();
    z80_io_port_out(IO_TIMER_PORT, 0x30);
    assert(z80_io_port_in(IO_TIMER_PORT) == 0);
    assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(z80_io_port_in(4) == (uint8_t)expected[i]);
  }
  assert(z80_io_port_in(2) == IO_SERVICE_READY);

  send(IO_SERVICE_UTILITY, version, sizeof(version));
  z80_io_ports_core1_service();
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  assert(z80_io_port_in(4) == sizeof(expected) - 1);
  reset();
  const uint8_t random[] = {0};
  call(IO_SERVICE_UTILITY, random, sizeof(random), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 2 && reply[0] == 0x34 && reply[1] == 0x12);
}

static void test_timers(void) {
  now = 1000;
  z80_io_port_out(0x17, 0x10);
  z80_io_port_out(0x17, 0x34);
  z80_io_port_out(0x17, 0x12);
  z80_io_port_out(0x17, 0x20);
  assert(z80_io_port_in(0x17) == 1);
  now += 0x1234;
  assert(z80_io_port_in(0x17) == 0);
  z80_io_port_out(0x17, 0x40);
  now += 0x12345678;
  z80_io_port_out(0x17, 0x60);
  for (unsigned i = 0; i < 4; ++i) {
    now += 100;
    assert(z80_io_port_in(0x17) == (uint8_t)(0x12345678u >> (8 * i)));
  }
  assert(z80_io_port_in(0x17) == 0xff);
  z80_io_port_out(0x17, 0);
  assert(z80_io_port_in(0x17) == 0);
  z80_io_port_out(0x17, 0x10);
  assert(z80_io_port_in(0x17) == 0xff); /* interrupted parameter collection */
  z80_io_port_out(0x17, 0);

  uint8_t start[] = {0, 3, 2, 0, 0, 0};
  now = 1999;
  call(IO_SERVICE_TIMERS, start, sizeof(start), IO_SERVICE_COMPLETE);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  uint8_t query[] = {1, 3};
  now = 2999;
  call(IO_SERVICE_TIMERS, query, sizeof(query), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == 1);
  now = 3000;
  call(IO_SERVICE_TIMERS, query, sizeof(query), IO_SERVICE_RESPONSE_READY);
  assert(reply[0] == 0);
  start[3] = 1;
  error(IO_SERVICE_TIMERS, start, sizeof(start), IO_SERVICE_INVALID_PARAMETER);
  start[1] = 4;
  error(IO_SERVICE_TIMERS, start, sizeof(start), IO_SERVICE_INVALID_PARAMETER);
}

static void test_environment(void) {
  uint8_t set[] = {3, 2, 't', 'z', 3, 0, 'U', 'T', 'C'};
  uint8_t get[] = {2, 2, 'T', 'Z'};
  call(IO_SERVICE_ENVIRONMENT, set, sizeof(set), IO_SERVICE_COMPLETE);
  assert(writes == 2); /* Empty baseline and first value. */
  unsigned before = writes;
  call(IO_SERVICE_ENVIRONMENT, set, sizeof(set), IO_SERVICE_COMPLETE);
  assert(writes == before);
  z80_io_ports_init();
  call(IO_SERVICE_ENVIRONMENT, get, sizeof(get), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 3 && memcmp(reply, "UTC", 3) == 0);
  set[4] = 0;
  call(IO_SERVICE_ENVIRONMENT, set, 6, IO_SERVICE_COMPLETE);
  call(IO_SERVICE_ENVIRONMENT, get, sizeof(get), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 0);
  get[2] = 'X';
  error(IO_SERVICE_ENVIRONMENT, get, sizeof(get), IO_SERVICE_NOT_FOUND);
  set[4] = 3;
  set[6] = 0;
  error(IO_SERVICE_ENVIRONMENT, set, sizeof(set), IO_SERVICE_INVALID_PARAMETER);
  set[6] = 'U';
  error(IO_SERVICE_ENVIRONMENT, set, 8, IO_SERVICE_INVALID_LENGTH);
  cli("COUNT=10", IO_SERVICE_RESPONSE_READY);
  cli("COUNT+2", IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "COUNT=12\r\n") == 0);
  cli("COPY=COUNT", IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "COPY=12\r\n") == 0);
  cli("-I COUNT=99", IO_SERVICE_RESPONSE_READY);
  cli("COUNT", IO_SERVICE_RESPONSE_READY);
  assert(strstr((char *)reply, "12") != NULL);
  cli("TOTAL=COUNT+COPY", IO_SERVICE_RESPONSE_READY);
  assert(strstr((char *)reply, "24") != NULL);
  cli("-D COPY", IO_SERVICE_RESPONSE_READY);
  cli("EMPTY=ABSENT+1", IO_SERVICE_ERROR);
  cli("EMPTY=", IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "EMPTY=\r\n") == 0);
  cli("LIMIT=9223372036854775807", IO_SERVICE_RESPONSE_READY);
  cli("LIMIT+1", IO_SERVICE_ERROR);
  assert(reply[0] == IO_SERVICE_STORAGE_READ);
  cli("LIMIT=LIMIT+1", IO_SERVICE_ERROR);
  assert(reply[0] == IO_SERVICE_STORAGE_READ);
  cli("LIMIT=LIMIT+9999999999999999999999999", IO_SERVICE_ERROR);
  assert(reply[0] == IO_SERVICE_STORAGE_READ);
  cli("?", IO_SERVICE_RESPONSE_READY);
  assert(strstr((char *)reply, "Pico flash") != NULL);
  uint8_t clear[] = {7};
  call(IO_SERVICE_ENVIRONMENT, clear, 1, IO_SERVICE_COMPLETE);

  uint8_t large[4 + 3 + 255] = {3, 3, 'K', '0', '0', 255, 0};
  memset(large + 7, 'V', 255);
  for (unsigned i = 0; i < 14; ++i) {
    large[3] = (uint8_t)('A' + i);
    call(IO_SERVICE_ENVIRONMENT, large, sizeof(large), IO_SERVICE_COMPLETE);
  }
  large[3] = 'Z';
  error(IO_SERVICE_ENVIRONMENT, large, sizeof(large), IO_SERVICE_CAPACITY);
  uint8_t count[] = {6};
  call(IO_SERVICE_ENVIRONMENT, count, 1, IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "14") == 0); /* failed mutation rolled back */
  uint8_t list[] = {5};
  error(IO_SERVICE_ENVIRONMENT, list, 1, IO_SERVICE_CAPACITY);

  call(IO_SERVICE_ENVIRONMENT, clear, 1, IO_SERVICE_COMPLETE);
  uint8_t short_entry[] = {3, 3, 'K', '0', '0', 0, 0};
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < 128; ++i) {
    short_entry[3] = (uint8_t)hex[i >> 4];
    short_entry[4] = (uint8_t)hex[i & 15];
    call(IO_SERVICE_ENVIRONMENT, short_entry, sizeof(short_entry),
         IO_SERVICE_COMPLETE);
  }
  short_entry[2] = 'X';
  error(IO_SERVICE_ENVIRONMENT, short_entry, sizeof(short_entry),
        IO_SERVICE_CAPACITY);
  call(IO_SERVICE_ENVIRONMENT, count, 1, IO_SERVICE_RESPONSE_READY);
  assert(strcmp((char *)reply, "128") == 0);

  call(IO_SERVICE_ENVIRONMENT, clear, 1, IO_SERVICE_COMPLETE);
  uint8_t maximum_entry[4 + 16 + 255] = {3, 16};
  memset(maximum_entry + 2, 'K', 16);
  maximum_entry[18] = 255;
  maximum_entry[19] = 0;
  memset(maximum_entry + 20, 'V', 255);
  call(IO_SERVICE_ENVIRONMENT, maximum_entry, sizeof(maximum_entry),
       IO_SERVICE_COMPLETE);
  maximum_entry[0] = 2;
  call(IO_SERVICE_ENVIRONMENT, maximum_entry, 18, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 255 && reply[254] == 'V');
  maximum_entry[1] = 17;
  error(IO_SERVICE_ENVIRONMENT, maximum_entry, 19, IO_SERVICE_INVALID_PARAMETER);

  call(IO_SERVICE_ENVIRONMENT, clear, 1, IO_SERVICE_COMPLETE);
  uint8_t exact_reply_entry[4 + 2 + 251] = {3, 2, 'K', '0', 251, 0};
  memset(exact_reply_entry + 6, 'V', 251);
  for (unsigned i = 0; i < 8; ++i) {
    exact_reply_entry[3] = (uint8_t)('0' + i);
    call(IO_SERVICE_ENVIRONMENT, exact_reply_entry, sizeof(exact_reply_entry),
         IO_SERVICE_COMPLETE);
  }
  call(IO_SERVICE_ENVIRONMENT, list, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 2048 && reply[2046] == '\r' && reply[2047] == '\n');
  cli("X=", IO_SERVICE_RESPONSE_READY);
  error(IO_SERVICE_ENVIRONMENT, list, 1, IO_SERVICE_CAPACITY);
}

static void test_power_loss(void) {
  uint8_t old[32], recovered[32];
  size_t length;
  memset(test_flash + Z80_FLASH_ENV_OFFSET, 0xff, Z80_FLASH_ENV_BYTES);
  assert(z80_env_snapshot_save((uint8_t *)"A\0old\0", 6) == 0);
  memcpy(old, "A\0old\0", 6);
  uint8_t baseline[8192];
  memcpy(baseline, test_flash + Z80_FLASH_ENV_OFFSET, sizeof(baseline));
  for (unsigned cut = 0; cut < FLASH_SECTOR_SIZE; ++cut) {
    memcpy(test_flash + Z80_FLASH_ENV_OFFSET, baseline, sizeof(baseline));
    cut_after = (int)cut;
    assert(z80_env_snapshot_save((uint8_t *)"A\0new\0", 6) == -3);
    assert(z80_env_snapshot_load(recovered, sizeof(recovered), &length) == 0);
    assert(length == 6);
    assert(memcmp(old, recovered, 6) == 0 ||
           memcmp("A\0new\0", recovered, 6) == 0);
    if (cut < FLASH_SECTOR_SIZE - FLASH_PAGE_SIZE)
      assert(memcmp(old, recovered, 6) == 0);
  }

  cut_after = -1;
  assert(z80_env_snapshot_save((uint8_t *)"A\0new\0", 6) == 0);
  assert(z80_env_snapshot_load(recovered, sizeof(recovered), &length) == 0);
  assert(length == 6 && memcmp(recovered, "A\0new\0", 6) == 0);
  for (unsigned slot = 0; slot < 2; ++slot) {
    uint8_t *payload = test_flash + Z80_FLASH_ENV_OFFSET +
                       slot * FLASH_SECTOR_SIZE + FLASH_PAGE_SIZE;
    if (memcmp(payload, "A\0new\0", 6) == 0)
      payload[0] ^= 1;
  }
  assert(z80_env_snapshot_load(recovered, sizeof(recovered), &length) == 0);
  assert(length == 6 && memcmp(old, recovered, 6) == 0);
  memset(test_flash + Z80_FLASH_ENV_OFFSET, 0, Z80_FLASH_ENV_BYTES);
  assert(z80_env_snapshot_load(recovered, sizeof(recovered), &length) == -2);
}

static unsigned weather_starts;
static bool start_weather(const char *path) {
  assert(strstr(path, "&appid=testkey&units=metric"));
  ++weather_starts;
  return true;
}
static void cancel_weather(void) {}

static void test_weather_port_abi(void) {
  memset(test_flash + Z80_FLASH_ENV_OFFSET, 0xff, Z80_FLASH_ENV_BYTES);
  cli("OWM_KEY=testkey", IO_SERVICE_RESPONSE_READY);
  cli("OWM_LOCATION=Sydney,AU", IO_SERVICE_RESPONSE_READY);
  z80_weather_init(start_weather, cancel_weather);
  now = 0;
  z80_weather_poll(true);
  uint8_t status_request[] = {1};
  call(IO_SERVICE_WEATHER, status_request, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == Z80_WEATHER_NONE);
  uint8_t field[] = {0, 3};
  error(IO_SERVICE_WEATHER, field, 2, IO_SERVICE_UNAVAILABLE);
  now = 5000;
  z80_weather_poll(true);
  call(IO_SERVICE_WEATHER, status_request, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == Z80_WEATHER_FETCHING);
  static const char current[] =
      "{\"cod\":200,\"name\":\"Sydney\",\"main\":{\"temp\":20.1,"
      "\"feels_like\":19.8,\"humidity\":60},\"wind\":{\"speed\":1.2},"
      "\"weather\":[{\"main\":\"Clear\",\"description\":\"clear sky\"}]}";
  static const char forecast[] =
      "{\"cod\":\"200\",\"list\":[{\"main\":{\"temp\":21,\"feels_like\":20},"
      "\"weather\":[{\"main\":\"Clouds\",\"description\":\"few clouds\"}],"
      "\"dt_txt\":\"2026-10-22 03:00:00\"}]}";
  z80_weather_result(200, current, sizeof(current) - 1, NULL);
  z80_weather_poll(true);
  assert(weather_starts == 2);
  z80_weather_result(200, forecast, sizeof(forecast) - 1, NULL);
  call(IO_SERVICE_WEATHER, status_request, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == Z80_WEATHER_READY);
  static const char *expected[] = {
      "Sydney", "Clear",  "Clear sky",  "20", "60",
      "1",      "Clouds", "Few clouds", "21", "2026-10-22 03:00:00",
      "0",      "C",      "",           "20", "20",
  };
  for (unsigned i = 0; i < 15; ++i) {
    field[1] = (uint8_t)i;
    call(IO_SERVICE_WEATHER, field, 2, IO_SERVICE_RESPONSE_READY);
    assert(reply_length == strlen(expected[i]));
    assert(strcmp((const char *)reply, expected[i]) == 0);
  }
  field[1] = 15;
  error(IO_SERVICE_WEATHER, field, 2, IO_SERVICE_INVALID_PARAMETER);
  error(IO_SERVICE_WEATHER, field, 1, IO_SERVICE_INVALID_LENGTH);
  field[0] = 2;
  error(IO_SERVICE_WEATHER, field, 2, IO_SERVICE_UNKNOWN_OPERATION);
}

static void test_led(void) {
  uint8_t get[] = {5};
  uint8_t set[] = {4, 1};
  call(IO_SERVICE_UTILITY, get, sizeof(get), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == 0);
  send(IO_SERVICE_UTILITY, set, sizeof(set));
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY && !led_state);
  assert(finish() == IO_SERVICE_COMPLETE && led_state);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  reset();
  call(IO_SERVICE_UTILITY, get, sizeof(get), IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == 1);
  set[1] = 0;
  call(IO_SERVICE_UTILITY, set, sizeof(set), IO_SERVICE_COMPLETE);
  assert(!led_state);
  unsigned before = led_calls;
  set[1] = 2;
  error(IO_SERVICE_UTILITY, set, sizeof(set), IO_SERVICE_INVALID_PARAMETER);
  set[1] = 255;
  error(IO_SERVICE_UTILITY, set, sizeof(set), IO_SERVICE_INVALID_PARAMETER);
  error(IO_SERVICE_UTILITY, set, 1, IO_SERVICE_INVALID_LENGTH);
  uint8_t extra[] = {5, 0};
  error(IO_SERVICE_UTILITY, extra, sizeof(extra), IO_SERVICE_INVALID_LENGTH);
  extra[0] = 6;
  error(IO_SERVICE_UTILITY, extra, 1, IO_SERVICE_UNKNOWN_OPERATION);
  assert(led_calls == before);
  led_available = false;
  set[1] = 1;
  error(IO_SERVICE_UTILITY, set, sizeof(set), IO_SERVICE_UNAVAILABLE);
  error(IO_SERVICE_UTILITY, get, sizeof(get), IO_SERVICE_UNAVAILABLE);
  assert(!led_state);
  led_available = true;
}

static unsigned chat_starts, chat_cancels;
static bool start_chat(const z80_chat_endpoint_t *endpoint, const char *json,
                        size_t length) {
  assert(!endpoint->https && endpoint->port == 11434);
  assert(strcmp(endpoint->host, "192.168.1.26") == 0);
  assert(strcmp(endpoint->path, "/v1/chat/completions") == 0);
  assert(!endpoint->key[0]);
  assert(length == 2 && memcmp(json, "{}", 2) == 0);
  ++chat_starts;
  return true;
}
static void cancel_chat(void) { ++chat_cancels; }

static void test_chat_port_abi(void) {
  cli("CHAT_PROVIDER=compatible", IO_SERVICE_RESPONSE_READY);
  cli("CHAT_ENDPOINT=http://192.168.1.26:11434/v1/chat/completions",
      IO_SERVICE_RESPONSE_READY);
  cli("-I CHAT_OPENAI_KEY=dummy-never-forwarded", IO_SERVICE_RESPONSE_READY);
  z80_chat_init(start_chat, cancel_chat);
  z80_chat_network_poll(true, 1);
  uint8_t operation[] = {0, 0, 1};
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  operation[0] = 2;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  assert(chat_cancels == 2);
  uint8_t append[] = {1, '{', '}'};
  call(IO_SERVICE_CHAT, append, sizeof(append), IO_SERVICE_COMPLETE);
  operation[0] = 3;
  send(IO_SERVICE_CHAT, operation, 1);
  assert(chat_starts == 0 && z80_io_port_in(2) == IO_SERVICE_BUSY);
  assert(finish() == IO_SERVICE_COMPLETE && chat_starts == 1);
  operation[0] = 4;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 1 && reply[0] == Z80_CHAT_WAITING);
  operation[0] = 3;
  error(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_BUSY_ERROR);
  error(IO_SERVICE_CHAT, append, sizeof(append), IO_SERVICE_BUSY_ERROR);
  z80_io_port_out(IO_TIMER_PORT, 0x10);
  z80_io_port_out(IO_TIMER_PORT, 50);
  z80_io_port_out(IO_TIMER_PORT, 0);
  z80_io_port_out(IO_TIMER_PORT, 0x20);
  assert(z80_io_port_in(IO_TIMER_PORT) == 1);
  static const char wire[] =
      "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\\nworld\"}}]}\n\n"
      "data: [DONE]\n\n";
  size_t consumed = z80_chat_receive((const uint8_t *)wire, sizeof(wire) - 1);
  assert(consumed < sizeof(wire) && !z80_chat_active());
  operation[0] = 4;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply[0] == Z80_CHAT_DATA_READY);
  operation[0] = 5;
  call(IO_SERVICE_CHAT, operation, 3, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 14 && reply[0] == Z80_CHAT_EOF &&
         reply[1] == 11 && reply[2] == 0 && memcmp(reply + 3, "Hello\nworld", 11) == 0);
  operation[1] = 0xfd;
  operation[2] = 7;
  call(IO_SERVICE_CHAT, operation, 3, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 3 && reply[0] == Z80_CHAT_EOF && reply[1] == 0);
  operation[1] = 0xfe;
  error(IO_SERVICE_CHAT, operation, 3, IO_SERVICE_INVALID_PARAMETER);
  operation[1] = operation[2] = 0;
  error(IO_SERVICE_CHAT, operation, 3, IO_SERVICE_INVALID_PARAMETER);
  operation[1] = operation[2] = 255;
  error(IO_SERVICE_CHAT, operation, 3, IO_SERVICE_INVALID_PARAMETER);
  error(IO_SERVICE_CHAT, operation, 2, IO_SERVICE_INVALID_LENGTH);
  operation[0] = 6;
  error(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_UNKNOWN_OPERATION);
  operation[0] = 2;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  operation[0] = 0;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  operation[0] = 3;
  error(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_INVALID_STATE);
  append[1] = 0;
  error(IO_SERVICE_CHAT, append, sizeof(append), IO_SERVICE_INVALID_PARAMETER);
  uint8_t block[2048];
  memset(block, 'a', sizeof(block));
  block[0] = 1;
  for (unsigned i = 0; i < 4; ++i)
    call(IO_SERVICE_CHAT, block, sizeof(block), IO_SERVICE_COMPLETE);
  call(IO_SERVICE_CHAT, block, 4, IO_SERVICE_COMPLETE);
  error(IO_SERVICE_CHAT, block, 2, IO_SERVICE_CAPACITY);
  operation[0] = 0;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);

  /* RESET invalidates a mailbox reply, not the service's side effects. */
  send(IO_SERVICE_CHAT, (const uint8_t[]){1, '{', '}'}, 3);
  reset();
  assert(finish() == IO_SERVICE_READY);
  operation[0] = 3;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  assert(chat_starts == 2);
  operation[0] = 0;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  assert(!z80_chat_active());
  operation[0] = 2;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  assert(!z80_chat_active());
  operation[0] = 4;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply[0] == Z80_CHAT_EOF);
  call(IO_SERVICE_CHAT, (const uint8_t[]){1, '{', '}'}, 3, IO_SERVICE_COMPLETE);
  operation[0] = 3;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
  assert(chat_starts == 3 && z80_chat_active());
  operation[0] = 2;
  call(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_COMPLETE);
}

int main(void) {
  memset(test_flash, 0xff, sizeof(test_flash));
  z80_io_ports_init();
  test_transport();
  test_worker_response_lifetime();
  test_timers();
  test_led();
  test_environment();
  uint8_t operation[] = {0};
  call(IO_SERVICE_UTILITY, operation, 1, IO_SERVICE_RESPONSE_READY);
  assert(reply_length == 2 && reply[0] == 0x34 && reply[1] == 0x12);
  operation[0] = 1;
  error(IO_SERVICE_CLOCK, operation, 1, IO_SERVICE_CLOCK_UNSYNCED);
  error(IO_SERVICE_WEATHER, operation, 1, IO_SERVICE_UNAVAILABLE);
  error(IO_SERVICE_CHAT, operation, 1, IO_SERVICE_UNAVAILABLE);
  error(IO_SERVICE_SENSE_HAT, operation, 1, IO_SERVICE_UNAVAILABLE);
  uint8_t reboot[] = {2, 0};
  error(IO_SERVICE_UTILITY, reboot, 2, IO_SERVICE_REBOOT_CONFIRMATION);
  assert(disk_write_enabled);
  reboot[1] = 0xa5;
  flush_ok = false;
  error(IO_SERVICE_UTILITY, reboot, 2, IO_SERVICE_STORAGE_WRITE);
  assert(!z80_io_ports_reboot_requested());
  assert(disk_write_enabled);
  flush_ok = true;
  call(IO_SERVICE_UTILITY, reboot, 2, IO_SERVICE_COMPLETE);
  assert(!disk_write_enabled);
  assert(z80_io_ports_reboot_requested());
  assert(!z80_io_ports_reboot_requested());
  test_power_loss();
  test_weather_port_abi();
  test_chat_port_abi();
  puts("ROMLESS-SVC/1 transport, timers, ENV and power-loss tests passed");
}
