#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include "../terminal_queue.c"

enum { TRANSFER_BYTES = 262144 };

static uint8_t pattern(size_t position) {
  return (uint8_t)('A' + position % 26);
}

static void *cpu_worker(void *context) {
  (void)context;
  size_t sent = 0, received = 0;
  while (sent < TRANSFER_BYTES || received < TRANSFER_BYTES) {
    uint8_t status = z80_terminal_io_read(1);
    if (sent < TRANSFER_BYTES && (status & TERM_STATUS_TX_ROOM))
      z80_terminal_io_write(0, pattern(sent++));
    if (received < TRANSFER_BYTES && (status & TERM_STATUS_RX_READY))
      assert(z80_terminal_io_read(0) == pattern(received++));
  }
  return NULL;
}

static void *browser_worker(void *context) {
  (void)context;
  size_t sent = 0, received = 0;
  uint8_t data[256];
  while (sent < TRANSFER_BYTES || received < TRANSFER_BYTES) {
    size_t count = z80_terminal_supply(data, sizeof(data));
    for (size_t i = 0; i < count; ++i)
      assert(data[i] == pattern(received++));
    if (sent < TRANSFER_BYTES) {
      size_t bytes = TRANSFER_BYTES - sent;
      if (bytes > 64)
        bytes = 64;
      for (size_t i = 0; i < bytes; ++i)
        data[i] = pattern(sent + i);
      if (z80_terminal_browser_try_receive(data, bytes))
        sent += bytes;
    }
    sched_yield();
  }
  return NULL;
}

int main(void) {
  z80_terminal_queue_init();
  z80_terminal_usb_connected(true);
  z80_terminal_browser_connect();
  pthread_t cpu, browser;
  assert(pthread_create(&cpu, NULL, cpu_worker, NULL) == 0);
  assert(pthread_create(&browser, NULL, browser_worker, NULL) == 0);
  assert(pthread_join(cpu, NULL) == 0);
  assert(pthread_join(browser, NULL) == 0);
  assert(rx_queue.count == 0 && tx_queue.count == 0);
  assert(z80_terminal_rx_drop_count() == 0 && z80_terminal_tx_drop_count() == 0);

  uint8_t data[512];
  memset(data, 'X', sizeof(data));
  assert(z80_terminal_browser_try_receive(data, 128));
  for (size_t i = 0; i < sizeof(data); ++i)
    z80_terminal_io_write(0, data[i]);
  z80_terminal_usb_receive('U');
  z80_terminal_browser_disconnect();
  assert(rx_queue.count == 0 && tx_queue.count == 0);
  assert(z80_terminal_client_connected() && !z80_terminal_browser_connected());
  z80_terminal_usb_receive('N');
  assert(z80_terminal_io_read(0) == 'N');
  z80_terminal_io_write(0, 'R');
  assert(z80_terminal_usb_supply(data, sizeof(data)) == 1 && data[0] == 'R');
  assert(z80_terminal_rx_drop_count() == 0 && z80_terminal_tx_drop_count() == 0);
  puts("PASS: concurrent 256 KiB duplex, ring wrapping, full-queue handover and USB isolation");
}
