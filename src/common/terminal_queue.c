#include "z80sbc/terminal.h"

#include <string.h>

#include "pico/critical_section.h"
#include "pico/stdlib.h"

enum {
  TERM_DATA_PORT = 0x00,
  TERM_STATUS_PORT = 0x01,
  TERM_RX_DEPTH = 128,
  TERM_TX_DEPTH = 512,
  TERM_STATUS_RX_READY = 1u << 0,
  TERM_STATUS_TX_ROOM = 1u << 1,
  TERM_STATUS_CLIENT = 1u << 7,
};

typedef struct {
  uint8_t *data;
  size_t capacity;
  size_t head;
  size_t count;
  size_t peak;
} terminal_ring_t;

static uint8_t rx_data[TERM_RX_DEPTH];
static uint8_t tx_data[TERM_TX_DEPTH];
static terminal_ring_t rx_queue = {rx_data, TERM_RX_DEPTH, 0, 0, 0};
static terminal_ring_t tx_queue = {tx_data, TERM_TX_DEPTH, 0, 0, 0};
static critical_section_t terminal_lock;
static uint32_t client_connected;
static uint32_t browser_connected;
static bool usb_connected;
static uint32_t rx_drops;
static uint32_t tx_drops;

static void clear_queues(void) {
  rx_queue.head = rx_queue.count = 0;
  tx_queue.head = tx_queue.count = 0;
}

static void __not_in_flash_func(ring_add)(terminal_ring_t *ring, uint8_t value) {
  ring->data[(ring->head + ring->count++) & (ring->capacity - 1)] = value;
  if (ring->count > ring->peak)
    ring->peak = ring->count;
}

static size_t ring_peek(const terminal_ring_t *ring, uint8_t *data,
                        size_t capacity) {
  size_t count = capacity < ring->count ? capacity : ring->count;
  size_t first = ring->capacity - ring->head;
  if (first > count)
    first = count;
  memcpy(data, ring->data + ring->head, first);
  memcpy(data + first, ring->data, count - first);
  return count;
}

static size_t ring_remove(terminal_ring_t *ring, uint8_t *data,
                          size_t capacity) {
  size_t count = ring_peek(ring, data, capacity);
  ring->head = (ring->head + count) & (ring->capacity - 1);
  ring->count -= count;
  return count;
}

void z80_terminal_queue_init(void) {
  if (!critical_section_is_initialized(&terminal_lock))
    critical_section_init(&terminal_lock);
  clear_queues();
  rx_queue.peak = tx_queue.peak = 0;
  __atomic_store_n(&client_connected, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&browser_connected, 0, __ATOMIC_RELEASE);
  usb_connected = false;
  __atomic_store_n(&rx_drops, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&tx_drops, 0, __ATOMIC_RELAXED);
}

uint8_t __not_in_flash_func(z80_terminal_io_read)(uint8_t port) {
  if (port != TERM_DATA_PORT && port != TERM_STATUS_PORT)
    return 0xFF;
  critical_section_enter_blocking(&terminal_lock);
  uint8_t value = 0;
  if (port == TERM_DATA_PORT) {
    if (rx_queue.count) {
      value = rx_queue.data[rx_queue.head];
      rx_queue.head = (rx_queue.head + 1) & (TERM_RX_DEPTH - 1);
      --rx_queue.count;
    }
  } else {
    bool connected = z80_terminal_client_connected();
    if (rx_queue.count)
      value |= TERM_STATUS_RX_READY;
    if (!connected || tx_queue.count < TERM_TX_DEPTH)
      value |= TERM_STATUS_TX_ROOM;
    if (connected)
      value |= TERM_STATUS_CLIENT;
  }
  critical_section_exit(&terminal_lock);
  return value;
}

void __not_in_flash_func(z80_terminal_io_write)(uint8_t port, uint8_t value) {
  if (port != TERM_DATA_PORT)
    return;
  critical_section_enter_blocking(&terminal_lock);
  if (z80_terminal_client_connected()) {
    if (tx_queue.count < TERM_TX_DEPTH)
      ring_add(&tx_queue, value);
    else
      __atomic_fetch_add(&tx_drops, 1, __ATOMIC_RELAXED);
  }
  critical_section_exit(&terminal_lock);
}

void z80_terminal_receive(const uint8_t *data, size_t length) {
  if (data == NULL)
    return;
  for (size_t index = 0; index < length; ++index) {
    critical_section_enter_blocking(&terminal_lock);
    if (rx_queue.count == TERM_RX_DEPTH) {
      rx_queue.head = (rx_queue.head + 1) & (TERM_RX_DEPTH - 1);
      --rx_queue.count;
      __atomic_fetch_add(&rx_drops, 1, __ATOMIC_RELAXED);
    }
    ring_add(&rx_queue, data[index] == '\n' ? '\r' : data[index]);
    critical_section_exit(&terminal_lock);
  }
}

size_t z80_terminal_peek(uint8_t *data, size_t capacity) {
  if (data == NULL)
    return 0;
  critical_section_enter_blocking(&terminal_lock);
  size_t count = ring_peek(&tx_queue, data, capacity);
  critical_section_exit(&terminal_lock);
  return count;
}

size_t z80_terminal_supply(uint8_t *data, size_t capacity) {
  if (data == NULL)
    return 0;
  critical_section_enter_blocking(&terminal_lock);
  size_t count = ring_remove(&tx_queue, data, capacity);
  critical_section_exit(&terminal_lock);
  return count;
}

bool z80_terminal_browser_try_receive(const uint8_t *data, size_t length) {
  if (data == NULL || length == 0 || length > TERM_RX_DEPTH)
    return false;
  critical_section_enter_blocking(&terminal_lock);
  bool accepted = z80_terminal_browser_connected() &&
                  TERM_RX_DEPTH - rx_queue.count >= length;
  if (accepted)
    for (size_t i = 0; i < length; ++i)
      ring_add(&rx_queue, data[i] == '\n' ? '\r' : data[i]);
  critical_section_exit(&terminal_lock);
  return accepted;
}

void z80_terminal_browser_connect(void) {
  critical_section_enter_blocking(&terminal_lock);
  clear_queues();
  __atomic_store_n(&browser_connected, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&client_connected, 1, __ATOMIC_RELEASE);
  critical_section_exit(&terminal_lock);
}

void z80_terminal_set_connected(bool connected) {
  critical_section_enter_blocking(&terminal_lock);
  __atomic_store_n(&client_connected, connected, __ATOMIC_RELEASE);
  if (!connected)
    tx_queue.head = tx_queue.count = 0;
  critical_section_exit(&terminal_lock);
}

void z80_terminal_browser_disconnect(void) {
  critical_section_enter_blocking(&terminal_lock);
  if (z80_terminal_browser_connected()) {
    __atomic_store_n(&browser_connected, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&client_connected, usb_connected, __ATOMIC_RELEASE);
    clear_queues();
  }
  critical_section_exit(&terminal_lock);
}

bool z80_terminal_browser_connected(void) {
  return __atomic_load_n(&browser_connected, __ATOMIC_ACQUIRE) != 0;
}

void z80_terminal_usb_connected(bool connected) {
  critical_section_enter_blocking(&terminal_lock);
  if (!z80_terminal_browser_connected()) {
    __atomic_store_n(&client_connected, connected, __ATOMIC_RELEASE);
    if (usb_connected != connected)
      clear_queues();
  }
  usb_connected = connected;
  critical_section_exit(&terminal_lock);
}

void z80_terminal_usb_receive(uint8_t value) {
  critical_section_enter_blocking(&terminal_lock);
  if (usb_connected && !z80_terminal_browser_connected()) {
    if (rx_queue.count == TERM_RX_DEPTH) {
      rx_queue.head = (rx_queue.head + 1) & (TERM_RX_DEPTH - 1);
      --rx_queue.count;
      __atomic_fetch_add(&rx_drops, 1, __ATOMIC_RELAXED);
    }
    ring_add(&rx_queue, value == '\n' ? '\r' : value);
  }
  critical_section_exit(&terminal_lock);
}

size_t z80_terminal_usb_supply(uint8_t *data, size_t capacity) {
  if (data == NULL)
    return 0;
  critical_section_enter_blocking(&terminal_lock);
  size_t count = usb_connected && !z80_terminal_browser_connected()
                     ? ring_remove(&tx_queue, data, capacity) : 0;
  critical_section_exit(&terminal_lock);
  return count;
}

uint32_t z80_terminal_rx_drop_count(void) {
  return __atomic_load_n(&rx_drops, __ATOMIC_RELAXED);
}

uint32_t z80_terminal_tx_drop_count(void) {
  return __atomic_load_n(&tx_drops, __ATOMIC_RELAXED);
}

z80_terminal_buffer_stats_t z80_terminal_get_buffer_stats(void) {
  critical_section_enter_blocking(&terminal_lock);
  z80_terminal_buffer_stats_t stats = {
      (uint32_t)rx_queue.count, TERM_RX_DEPTH, (uint32_t)rx_queue.peak,
      (uint32_t)tx_queue.count, TERM_TX_DEPTH, (uint32_t)tx_queue.peak,
  };
  critical_section_exit(&terminal_lock);
  return stats;
}

bool __not_in_flash_func(z80_terminal_client_connected)(void) {
  return __atomic_load_n(&client_connected, __ATOMIC_ACQUIRE) != 0;
}
