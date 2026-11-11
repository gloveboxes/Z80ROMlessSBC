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

/* The count and drops change only under log_lock, but are stored atomically
 * so core 1's idle passes read them without the lock core 0 logs under. */
static inline size_t log_pending(void) {
  return __atomic_load_n(&log_count, __ATOMIC_RELAXED);
}

static inline void log_set_count(size_t count) {
  __atomic_store_n(&log_count, count, __ATOMIC_RELAXED);
}

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
      __atomic_store_n(&log_drops, log_drops + 1, __ATOMIC_RELAXED);
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
      log_set_count(log_count + needed);
    }
  }
  critical_section_exit(&log_lock);
}

/* Output only: core 1 reads the endpoint through z80_usb_console_read(). */
static stdio_driver_t console_driver = {
  .out_chars = log_write,
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
  return __atomic_load_n(&log_drops, __ATOMIC_RELAXED);
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
  if (connected != usb_connected) {
    critical_section_enter_blocking(&log_lock);
    usb_connected = connected;
    if (!connected) {
      log_head = 0;
      log_set_count(0);
      last_log_cr = false;
    }
    critical_section_exit(&log_lock);
  }
  if (!connected)
    return;
  size_t count = log_pending() ? tud_cdc_write_available() : 0;
  if (count) {
    char data[LOG_BUDGET];
    if (count > sizeof(data))
      count = sizeof(data);
    critical_section_enter_blocking(&log_lock);
    /* Only core 1 consumes this queue, so the count can only have grown. */
    if (count > log_count)
      count = log_count;
    size_t first = sizeof(log_data) - log_head;
    if (first > count)
      first = count;
    memcpy(data, log_data + log_head, first);
    memcpy(data + first, log_data, count - first);
    critical_section_exit(&log_lock);
    /* Producers append behind the snapshot; retain its unaccepted suffix
     * when the endpoint applies backpressure. */
    size_t accepted = tud_cdc_write(data, (uint32_t)count);
    critical_section_enter_blocking(&log_lock);
    log_head = (log_head + accepted) % sizeof(log_data);
    log_set_count(log_count - accepted);
    critical_section_exit(&log_lock);
    if (accepted)
      tud_cdc_write_flush();
  }
  uint32_t drops = z80_usb_console_log_drops();
  if (drops != reported_drops) {
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
      log_set_count(log_count + (size_t)length);
      last_log_cr = false;
      reported_drops = drops;
    }
    critical_section_exit(&log_lock);
  }
}

size_t z80_usb_console_read(uint8_t *data, size_t capacity) {
  assert(get_core_num() == 1);
  int count = stdio_usb.in_chars((char *)data, (int)capacity);
  return count > 0 ? (size_t)count : 0;
}

size_t z80_usb_console_write(const uint8_t *data, size_t length) {
  assert(get_core_num() == 1);
  /* Do not splice terminal bytes into a queued diagnostic line. */
  if (!stdio_usb_connected() || log_pending())
    return 0;
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
