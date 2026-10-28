#include "usb_console.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/critical_section.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

enum { LOG_BYTES = 4096, LOG_BUDGET = 128, TERMINAL_FLUSH_US = 250 };
static critical_section_t log_lock;
static char log_data[LOG_BYTES];
static size_t log_head, log_count;
static uint32_t log_drops;
static bool usb_connected;
static bool last_log_cr;
static uint32_t reported_drops;
static bool terminal_flush_pending;
static uint64_t terminal_flush_deadline;

static void log_write(const char *data, int length) {
  critical_section_enter_blocking(&log_lock);
  if (usb_connected && length > 0) {
    size_t needed = (size_t)length;
    size_t room = sizeof(log_data) - log_count;
    if (needed <= room) {
#if PICO_STDIO_USB_DEFAULT_CRLF
      bool previous_cr = last_log_cr;
      for (int i = 0; i < length; ++i) {
        if (data[i] == '\n' && !previous_cr)
          ++needed;
        previous_cr = data[i] == '\r';
      }
#endif
    }
    if (needed > room) {
      ++log_drops;
    } else {
      size_t head = (log_head + log_count) % sizeof(log_data);
      /* Translate under this lock, not the SDK's shared CRLF state. */
      for (int i = 0; i < length; ++i) {
#if PICO_STDIO_USB_DEFAULT_CRLF
        if (data[i] == '\n' && !last_log_cr) {
          log_data[head] = '\r';
          head = (head + 1) % sizeof(log_data);
        }
#endif
        log_data[head] = data[i];
        head = (head + 1) % sizeof(log_data);
        last_log_cr = data[i] == '\r';
      }
      log_count += needed;
    }
  }
  critical_section_exit(&log_lock);
}

static int usb_read(char *data, int length) {
  assert(get_core_num() == 1);
  return stdio_usb.in_chars(data, length);
}

static stdio_driver_t console_driver = {
  .out_chars = log_write,
  .in_chars = usb_read,
};

void z80_usb_console_prepare(void) {
  assert(get_core_num() == 0);
  critical_section_init(&log_lock);
  stdio_set_driver_enabled(&console_driver, true);
}

bool z80_usb_console_init(void) {
  assert(get_core_num() == 1);
  alarm_pool_init_default();
  bool initialized = stdio_usb_init();
  /* Only the queue driver is visible to printf, including lower-level errors. */
  stdio_set_driver_enabled(&stdio_usb, false);
  return initialized;
}

uint32_t z80_usb_console_log_drops(void) {
  critical_section_enter_blocking(&log_lock);
  uint32_t count = log_drops;
  critical_section_exit(&log_lock);
  return count;
}

void z80_usb_console_service(void) {
  assert(get_core_num() == 1);
  bool connected = stdio_usb_connected();
  if (terminal_flush_pending &&
      (!connected || time_us_64() >= terminal_flush_deadline)) {
    if (connected)
      tud_cdc_write_flush();
    terminal_flush_pending = false;
  }
  char data[LOG_BUDGET];
  size_t count = connected ? tud_cdc_write_available() : 0;
  if (count > sizeof(data))
    count = sizeof(data);
  critical_section_enter_blocking(&log_lock);
  usb_connected = connected;
  if (!connected) {
    log_head = log_count = 0;
    last_log_cr = false;
  }
  if (count > log_count)
    count = log_count;
  size_t first = sizeof(log_data) - log_head;
  if (first > count)
    first = count;
  memcpy(data, log_data + log_head, first);
  memcpy(data + first, log_data, count - first);
  uint32_t drops = log_drops;
  critical_section_exit(&log_lock);
  if (count) {
    /* Only core 1 consumes this queue. Producers append behind the snapshot;
     * retain its unaccepted suffix when the endpoint applies backpressure. */
    size_t accepted = tud_cdc_write(data, (uint32_t)count);
    critical_section_enter_blocking(&log_lock);
    log_head = (log_head + accepted) % sizeof(log_data);
    log_count -= accepted;
    critical_section_exit(&log_lock);
    if (accepted)
      tud_cdc_write_flush();
  }
  if (connected && drops != reported_drops) {
    char message[96];
    int length = snprintf(message, sizeof(message),
                          "\r\n[diag] FAIL: USB log queue overflow; log_drops=%lu\r\n",
                          (unsigned long)drops);
    /* This message already contains CRLF. Queue it only when it fits;
     * backpressure must not generate further overflow reports itself. */
    critical_section_enter_blocking(&log_lock);
    if ((size_t)length <= sizeof(log_data) - log_count) {
      size_t head = (log_head + log_count) % sizeof(log_data);
      size_t first = sizeof(log_data) - head;
      if (first > (size_t)length)
        first = (size_t)length;
      memcpy(log_data + head, message, first);
      memcpy(log_data, message + first, (size_t)length - first);
      log_count += (size_t)length;
      last_log_cr = false;
      reported_drops = drops;
    }
    critical_section_exit(&log_lock);
  }
}

size_t z80_usb_console_write(const uint8_t *data, size_t length) {
  assert(get_core_num() == 1);
  if (!stdio_usb_connected())
    return 0;
  critical_section_enter_blocking(&log_lock);
  bool logs_pending = log_count != 0;
  critical_section_exit(&log_lock);
  if (logs_pending)
    return 0; /* Do not splice terminal bytes into a queued diagnostic line. */
  size_t room = tud_cdc_write_available();
  if (length > room)
    length = room;
  if (!length)
    return 0;
  size_t accepted = tud_cdc_write(data, (uint32_t)length);
  if (accepted) {
    if (memchr(data, '\n', accepted) != NULL) {
      tud_cdc_write_flush();
      terminal_flush_pending = false;
    } else if (!terminal_flush_pending) {
      terminal_flush_deadline = time_us_64() + TERMINAL_FLUSH_US;
      terminal_flush_pending = true;
    }
  }
  return accepted;
}
