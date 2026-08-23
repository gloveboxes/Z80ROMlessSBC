#include "z80sbc/terminal.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"

enum {
  TERM_DATA_PORT = 0x00,
  TERM_STATUS_PORT = 0x01,
  TERM_RX_DEPTH = 128,
  TERM_TX_DEPTH = 512,
  TERM_STATUS_RX_READY = 1u << 0,
  TERM_STATUS_TX_ROOM = 1u << 1,
  TERM_STATUS_CLIENT = 1u << 7,
  WS_OUTPUT_INTERVAL_MS = 20,
  WS_INPUT_INTERVAL_MS = 10,
};

bool z80_terminal_network_poll(void);
void z80_terminal_network_poll_input(void);
void z80_terminal_network_poll_output(void);

static queue_t rx_queue;
static queue_t tx_queue;
static uint32_t client_connected;
static uint32_t rx_drops;
static uint32_t tx_drops;
static uint32_t pending_output;
static uint32_t pending_input;
static repeating_timer_t output_timer;
static repeating_timer_t input_timer;

static bool output_timer_callback(repeating_timer_t *timer) {
  (void)timer;
  __atomic_store_n(&pending_output, 1, __ATOMIC_RELEASE);
  return true;
}

static bool input_timer_callback(repeating_timer_t *timer) {
  (void)timer;
  __atomic_store_n(&pending_input, 1, __ATOMIC_RELEASE);
  return true;
}

bool z80_terminal_init(void) {
  queue_init(&rx_queue, sizeof(uint8_t), TERM_RX_DEPTH);
  queue_init(&tx_queue, sizeof(uint8_t), TERM_TX_DEPTH);
  __atomic_store_n(&client_connected, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&rx_drops, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&tx_drops, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&pending_output, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&pending_input, 0, __ATOMIC_RELEASE);

  if (!add_repeating_timer_ms(-WS_OUTPUT_INTERVAL_MS, output_timer_callback,
                              NULL, &output_timer))
    return false;
  if (!add_repeating_timer_ms(-WS_INPUT_INTERVAL_MS, input_timer_callback,
                              NULL, &input_timer)) {
    cancel_repeating_timer(&output_timer);
    return false;
  }
  return true;
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
  if (queue_get_level(&tx_queue) < TERM_TX_DEPTH)
    status |= TERM_STATUS_TX_ROOM;
  if (__atomic_load_n(&client_connected, __ATOMIC_ACQUIRE))
    status |= TERM_STATUS_CLIENT;
  return status;
}

void z80_terminal_io_write(uint8_t port, uint8_t value) {
  if (port != TERM_DATA_PORT)
    return;
  if (!queue_try_add(&tx_queue, &value))
    __atomic_fetch_add(&tx_drops, 1, __ATOMIC_RELAXED);
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

void z80_terminal_network_connected(void) {
  __atomic_store_n(&client_connected, 1, __ATOMIC_RELEASE);
}

void z80_terminal_network_disconnected(void) {
  __atomic_store_n(&client_connected, 0, __ATOMIC_RELEASE);
  uint8_t discard;
  while (queue_try_remove(&rx_queue, &discard)) {}
  while (queue_try_remove(&tx_queue, &discard)) {}
}

void z80_terminal_network_tx_dropped(size_t count) {
  __atomic_fetch_add(&tx_drops, (uint32_t)count, __ATOMIC_RELAXED);
}

void z80_terminal_core1_service(void) {
  bool ready = z80_terminal_network_poll();
  if (ready &&
      __atomic_exchange_n(&pending_input, 0, __ATOMIC_ACQ_REL))
    z80_terminal_network_poll_input();
  if (ready &&
      __atomic_exchange_n(&pending_output, 0, __ATOMIC_ACQ_REL))
    z80_terminal_network_poll_output();
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