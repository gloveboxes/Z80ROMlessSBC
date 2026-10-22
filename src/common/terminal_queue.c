#include "z80sbc/terminal.h"

#include "pico/critical_section.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

enum {
  TERM_DATA_PORT = 0x00,
  TERM_STATUS_PORT = 0x01,
  TERM_RX_DEPTH = 128,
  TERM_TX_DEPTH = 512,
  TERM_STATUS_RX_READY = 1u << 0,
  TERM_STATUS_TX_ROOM = 1u << 1,
  TERM_STATUS_CLIENT = 1u << 7,
};

static queue_t rx_queue;
static queue_t tx_queue;
static critical_section_t tx_state_lock;
static uint32_t client_connected;
static uint32_t browser_connected;
static bool usb_connected;
static uint32_t rx_drops;
static uint32_t tx_drops;
void z80_terminal_queue_init(void) {
  if (!critical_section_is_initialized(&tx_state_lock))
    critical_section_init(&tx_state_lock);
  queue_init(&rx_queue, sizeof(uint8_t), TERM_RX_DEPTH);
  queue_init(&tx_queue, sizeof(uint8_t), TERM_TX_DEPTH);
  __atomic_store_n(&client_connected, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&browser_connected, 0, __ATOMIC_RELEASE);
  usb_connected = false;
  __atomic_store_n(&rx_drops, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&tx_drops, 0, __ATOMIC_RELAXED);
}

uint8_t z80_terminal_io_read(uint8_t port) {
  if (port == TERM_DATA_PORT) {
    uint8_t value = 0;
    queue_try_remove(&rx_queue, &value);
    return value;
  }
  if (port != TERM_STATUS_PORT)
    return 0xFF;

  uint8_t status = 0;
  if (queue_get_level(&rx_queue) != 0)
    status |= TERM_STATUS_RX_READY;
  critical_section_enter_blocking(&tx_state_lock);
  bool connected = z80_terminal_client_connected();
  if (!connected || queue_get_level(&tx_queue) < TERM_TX_DEPTH)
    status |= TERM_STATUS_TX_ROOM;
  if (connected)
    status |= TERM_STATUS_CLIENT;
  critical_section_exit(&tx_state_lock);
  return status;
}

void z80_terminal_io_write(uint8_t port, uint8_t value) {
  if (port != TERM_DATA_PORT)
    return;
  critical_section_enter_blocking(&tx_state_lock);
  if (z80_terminal_client_connected() && !queue_try_add(&tx_queue, &value))
    __atomic_fetch_add(&tx_drops, 1, __ATOMIC_RELAXED);
  critical_section_exit(&tx_state_lock);
}

void z80_terminal_network_receive(const uint8_t *data, size_t length) {
  if (data == NULL)
    return;
  for (size_t index = 0; index < length; ++index) {
    uint8_t value = data[index] == '\n' ? '\r' : data[index];
    if (!queue_try_add(&rx_queue, &value)) {
      uint8_t discard;
      if (queue_try_remove(&rx_queue, &discard))
        __atomic_fetch_add(&rx_drops, 1, __ATOMIC_RELAXED);
      if (!queue_try_add(&rx_queue, &value))
        __atomic_fetch_add(&rx_drops, 1, __ATOMIC_RELAXED);
    }
  }
}

size_t z80_terminal_network_supply(uint8_t *data, size_t capacity) {
  if (data == NULL)
    return 0;
  size_t count = 0;
  while (count < capacity && queue_try_remove(&tx_queue, &data[count]))
    ++count;
  return count;
}

bool z80_terminal_network_try_receive(const uint8_t *data, size_t length) {
  if (data == NULL || length == 0 || length > TERM_RX_DEPTH)
    return false;
  critical_section_enter_blocking(&tx_state_lock);
  bool accepted = z80_terminal_browser_connected() &&
                  TERM_RX_DEPTH - queue_get_level(&rx_queue) >= length;
  if (accepted) {
    // USB is excluded by this lock; core 0 can only free RX space.
    for (size_t i = 0; i < length; ++i) {
      uint8_t value = data[i] == '\n' ? '\r' : data[i];
      queue_try_add(&rx_queue, &value);
    }
  }
  critical_section_exit(&tx_state_lock);
  return accepted;
}

void z80_terminal_network_connected(void) {
  critical_section_enter_blocking(&tx_state_lock);
  uint8_t discard;
  while (queue_try_remove(&rx_queue, &discard)) {}
  while (queue_try_remove(&tx_queue, &discard)) {}
  __atomic_store_n(&browser_connected, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&client_connected, 1, __ATOMIC_RELEASE);
  critical_section_exit(&tx_state_lock);
}

void z80_terminal_set_connected(bool connected) {
  // Serialize disconnect cleanup with the Z80 producer on the other core.
  critical_section_enter_blocking(&tx_state_lock);
  __atomic_store_n(&client_connected, connected, __ATOMIC_RELEASE);
  if (!connected) {
    uint8_t discard;
    while (queue_try_remove(&tx_queue, &discard)) {}
  }
  critical_section_exit(&tx_state_lock);
}

void z80_terminal_network_disconnected(void) {
  critical_section_enter_blocking(&tx_state_lock);
  if (!z80_terminal_browser_connected()) {
    critical_section_exit(&tx_state_lock);
    return;
  }
  __atomic_store_n(&browser_connected, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&client_connected, usb_connected, __ATOMIC_RELEASE);
  uint8_t discard;
  while (queue_try_remove(&rx_queue, &discard)) {}
  while (queue_try_remove(&tx_queue, &discard)) {}
  critical_section_exit(&tx_state_lock);
}

bool z80_terminal_browser_connected(void) {
  return __atomic_load_n(&browser_connected, __ATOMIC_ACQUIRE) != 0;
}

void z80_terminal_usb_connected(bool connected) {
  critical_section_enter_blocking(&tx_state_lock);
  if (!z80_terminal_browser_connected()) {
    __atomic_store_n(&client_connected, connected, __ATOMIC_RELEASE);
    if (usb_connected != connected) {
      uint8_t discard;
      while (queue_try_remove(&rx_queue, &discard)) {}
      while (queue_try_remove(&tx_queue, &discard)) {}
    }
  }
  usb_connected = connected;
  critical_section_exit(&tx_state_lock);
}

void z80_terminal_usb_receive(uint8_t value) {
  critical_section_enter_blocking(&tx_state_lock);
  if (usb_connected && !z80_terminal_browser_connected())
    z80_terminal_network_receive(&value, 1);
  critical_section_exit(&tx_state_lock);
}

size_t z80_terminal_usb_supply(uint8_t *data, size_t capacity) {
  critical_section_enter_blocking(&tx_state_lock);
  size_t count = usb_connected && !z80_terminal_browser_connected()
                     ? z80_terminal_network_supply(data, capacity) : 0;
  critical_section_exit(&tx_state_lock);
  return count;
}

void z80_terminal_network_tx_dropped(size_t count) {
  __atomic_fetch_add(&tx_drops, (uint32_t)count, __ATOMIC_RELAXED);
}

uint32_t z80_terminal_rx_drop_count(void) {
  return __atomic_load_n(&rx_drops, __ATOMIC_RELAXED);
}

uint32_t z80_terminal_tx_drop_count(void) {
  return __atomic_load_n(&tx_drops, __ATOMIC_RELAXED);
}

bool z80_terminal_client_connected(void) {
  return __atomic_load_n(&client_connected, __ATOMIC_ACQUIRE) != 0;
}