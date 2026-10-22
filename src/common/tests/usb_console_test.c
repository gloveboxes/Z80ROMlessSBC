#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include "../../stage09_flash_storage/usb_console.c"

static _Thread_local uint core;
static bool connected, alarm_ready, init_ok = true;
static unsigned usb_inits, room, written;
static char output[16384];
static stdio_driver_t *registered;
static bool streaming_test;
static uint32_t producer_done;
enum { TRANSFER_LINES = 10000 };

uint get_core_num(void) { return core; }
void alarm_pool_init_default(void) {
  assert(core == 1);
  alarm_ready = true;
}
void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled) {
  if (enabled)
    registered = driver;
  else if (registered == driver)
    registered = &console_driver;
}
bool stdio_usb_init(void) {
  assert(core == 1 && alarm_ready);
  ++usb_inits;
  stdio_set_driver_enabled(&stdio_usb, true);
  return init_ok;
}
bool stdio_usb_connected(void) {
  assert(core == 1);
  return connected;
}
uint32_t tud_cdc_write_available(void) {
  assert(core == 1);
  return room;
}
static void usb_write(const char *data, int length) {
  assert(core == 1 && connected && (unsigned)length <= room);
  if (streaming_test) {
    static const char expected[] = "C\r\n";
    for (int i = 0; i < length; ++i)
      assert(data[i] == expected[written++ % 3]);
    room -= (unsigned)length;
    return;
  }
  assert(written + (unsigned)length < sizeof(output));
  memcpy(output + written, data, (size_t)length);
  written += (unsigned)length;
  output[written] = '\0';
  room -= (unsigned)length;
}
static int usb_input(char *data, int length) {
  assert(core == 1 && length > 0);
  data[0] = 'K';
  return 1;
}
stdio_driver_t stdio_usb = {
  .out_chars = usb_write,
  .in_chars = usb_input,
};

static void *cpu_logger(void *context) {
  (void)context;
  assert(core == 0);
  for (unsigned i = 0; i < TRANSFER_LINES;) {
    critical_section_enter_blocking(&log_lock);
    bool available = log_count <= LOG_BYTES - 3;
    critical_section_exit(&log_lock);
    if (available) {
      registered->out_chars("C\n", 2);
      ++i;
    } else {
      sched_yield();
    }
  }
  __atomic_store_n(&producer_done, 1, __ATOMIC_RELEASE);
  return NULL;
}

static void test_concurrent_diagnostics(void) {
  streaming_test = true;
  written = 0;
  pthread_t producer;
  assert(pthread_create(&producer, NULL, cpu_logger, NULL) == 0);
  while (true) {
    room = LOG_BUDGET;
    z80_usb_console_service();
    bool done = __atomic_load_n(&producer_done, __ATOMIC_ACQUIRE);
    critical_section_enter_blocking(&log_lock);
    bool empty = log_count == 0;
    critical_section_exit(&log_lock);
    if (done && empty)
      break;
    sched_yield();
  }
  assert(pthread_join(producer, NULL) == 0);
  assert(written == TRANSFER_LINES * 3 && z80_usb_console_log_drops() == 1);
}

int main(void) {
  z80_usb_console_prepare();
  assert(registered == &console_driver && !usb_inits);
  core = 1;
  assert(z80_usb_console_init() && usb_inits == 1);
  assert(registered == &console_driver);
  connected = true;
  room = 4096;
  z80_usb_console_service();
  char data[LOG_BYTES];
  memset(data, 'A', sizeof(data));
  core = 0;
  registered->out_chars(data, 3000);
  assert(!written && log_count == 3000);
  core = 1;
  room = 0;
  z80_usb_console_service();
  assert(!written && log_count == 3000);
  room = 4096;
  z80_usb_console_service();
  assert(written == LOG_BUDGET && log_count == 3000 - LOG_BUDGET);
  while (log_count)
    z80_usb_console_service();
  assert(written == 3000);
  core = 0;
  registered->out_chars(data, LOG_BYTES);
  registered->out_chars("overflow", 8);
  assert(z80_usb_console_log_drops() == 1 && log_count == LOG_BYTES);
  core = 1;
  room = 8192;
  while (log_count)
    z80_usb_console_service();
  assert(strstr(output, "FAIL: USB log queue overflow; log_drops=1"));
  assert(reported_drops == 1);
  char key;
  assert(registered->in_chars(&key, 1) == 1 && key == 'K');
  z80_usb_console_put('Z');
  assert(output[written - 1] == 'Z');
  registered->out_chars("stale", 5);
  connected = false;
  z80_usb_console_service();
  assert(!log_count);
  core = 0;
  registered->out_chars("discard while disconnected", 26);
  assert(!log_count && z80_usb_console_log_drops() == 1);
  core = 1;
  connected = true;
  z80_usb_console_service();
  unsigned previous = written;
  registered->out_chars("fresh", 5);
  z80_usb_console_service();
  assert(written == previous + 5 && !strcmp(output + previous, "fresh"));
  previous = written;
  core = 0;
  registered->out_chars("line1\nline2\r", 12);
  registered->out_chars("\n", 1);
  core = 1;
  z80_usb_console_service();
  assert(!strcmp(output + previous, "line1\r\nline2\r\n"));
  previous = written;
  z80_usb_console_put('\n');
  assert(written == previous + 1 && output[previous] == '\n');
  test_concurrent_diagnostics();
  init_ok = false;
  assert(!z80_usb_console_init());
  assert(registered == &console_driver);
  puts("PASS: core-1 USB/alarm ownership, bounded diagnostics, wrap, backpressure, "
       "overflow reporting, disconnect and initialization failure");
  return 0;
}
