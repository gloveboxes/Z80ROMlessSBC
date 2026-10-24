#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "matrix_i2c.pio.h"
#include "matrix_display.h"
#include "pico/stdlib.h"

namespace {
constexpr uint PIN_SCL = 26;
constexpr uint PIN_SDA = 22;
constexpr uint8_t I2C_CHUNK_BYTES = 8;
constexpr uint8_t FIRST_SCANNED_ADDRESS = 0x70;
constexpr uint8_t LAST_SCANNED_ADDRESS = 0x77;
constexpr uint64_t PIO_TIMEOUT_US = 5000;
constexpr uint64_t OFFLINE_ROW_INTERVAL_US = 500'000;
constexpr uint64_t ONLINE_ROW_INTERVAL_US = 250'000;
constexpr uint32_t PIO_CYCLES_PER_I2C_BIT = 12;
constexpr uint32_t I2C_FREQUENCY_HZ = 100'000;

enum class DisplayState : uint8_t {
  uninitialized,
  scan_addresses,
  initialize,
  ready,
  failed,
};

enum class DisplayFailure : uint8_t {
  none,
  no_program,
  no_state_machine,
  initialization_nack,
  ram_nack,
  scan_timeout,
  no_device,
};

constexpr const char *failure_reasons[] = {
    "none",
    "no PIO instruction memory for I2C program",
    "no PIO state machine available",
    "HT16K33 initialization was not acknowledged",
    "HT16K33 matrix RAM write was not acknowledged",
    "I2C transport timeout during address scan",
    "no HT16K33 ACK from address 0x70 through 0x77",
};

DisplayState state = DisplayState::uninitialized;
PIO pio = pio0;
int state_machine = -1;
uint offset;
uint8_t display_address;
uint8_t scan_address = FIRST_SCANNED_ADDRESS;
uint8_t initialization_index;
uint8_t matrix_row;
uint8_t matrix_chunk;
bool pio_timed_out;
DisplayFailure failure = DisplayFailure::none;
uint32_t published_status;
uint64_t next_matrix_update_us;
bool wifi_connected;

uint64_t matrix_update_interval_us() {
  return wifi_connected ? ONLINE_ROW_INTERVAL_US : OFFLINE_ROW_INTERVAL_US;
}

enum class TransferStep : uint8_t { idle, start, write, ack, stop, stop_ack };
enum class TransferResult : uint8_t { pending, complete, nack, timeout };
struct {
  TransferStep step = TransferStep::idle;
  uint8_t data[I2C_CHUNK_BYTES + 2] = {};
  size_t length = 0;
  size_t index = 0;
  bool acknowledged = true;
  uint64_t deadline = 0;
} transfer;

void begin_transfer(uint8_t address, uint8_t command,
                    const uint8_t *data, size_t length, bool probe = false) {
  transfer.data[0] = static_cast<uint8_t>(address << 1);
  transfer.data[1] = command;
  if (length)
    memcpy(transfer.data + 2, data, length);
  transfer.length = probe ? 1 : length + 2;
  transfer.index = 0;
  transfer.acknowledged = true;
  transfer.deadline = time_us_64() + PIO_TIMEOUT_US;
  transfer.step = TransferStep::start;
}

TransferResult service_transfer() {
  bool receiving = transfer.step == TransferStep::ack ||
                   transfer.step == TransferStep::stop_ack;
  bool waiting = receiving ? pio_sm_is_rx_fifo_empty(pio, state_machine)
                           : pio_sm_is_tx_fifo_full(pio, state_machine);
  if (waiting) {
    if (time_us_64() < transfer.deadline)
      return TransferResult::pending;
    pio_timed_out = true;
    return TransferResult::timeout;
  }
  switch (transfer.step) {
  case TransferStep::start:
    pio_sm_put(pio, state_machine, 1u << 30);
    transfer.step = TransferStep::write;
    break;
  case TransferStep::write:
    // Two action bits precede eight inverted, MSB-first pin-direction bits.
    pio_sm_put(pio, state_machine,
               static_cast<uint32_t>(
                   static_cast<uint8_t>(~transfer.data[transfer.index])) << 22);
    transfer.step = TransferStep::ack;
    break;
  case TransferStep::ack:
    transfer.acknowledged = (pio_sm_get(pio, state_machine) & 1u) == 0;
    ++transfer.index;
    transfer.step = transfer.acknowledged && transfer.index < transfer.length
                        ? TransferStep::write : TransferStep::stop;
    break;
  case TransferStep::stop:
    pio_sm_put(pio, state_machine, 2u << 30);
    transfer.step = TransferStep::stop_ack;
    break;
  case TransferStep::stop_ack:
    (void)pio_sm_get(pio, state_machine);
    transfer.step = TransferStep::idle;
    return transfer.acknowledged ? TransferResult::complete
                                 : TransferResult::nack;
  case TransferStep::idle:
    return TransferResult::pending;
  }
  transfer.deadline = time_us_64() + PIO_TIMEOUT_US;
  return TransferResult::pending;
}

bool initialize_bus() {
  if (!pio_can_add_program(pio, &matrix_i2c_program)) {
    failure = DisplayFailure::no_program;
    return false;
  }
  state_machine = pio_claim_unused_sm(pio, false);
  if (state_machine < 0) {
    failure = DisplayFailure::no_state_machine;
    return false;
  }

  offset = pio_add_program(pio, &matrix_i2c_program);
  pio_gpio_init(pio, PIN_SCL);
  pio_gpio_init(pio, PIN_SDA);

  pio_sm_config config = matrix_i2c_program_get_default_config(offset);
  sm_config_set_sideset_pins(&config, PIN_SCL);
  sm_config_set_out_pins(&config, PIN_SDA, 1);
  sm_config_set_set_pins(&config, PIN_SDA, 1);
  sm_config_set_in_pins(&config, PIN_SDA);
  sm_config_set_out_shift(&config, false, false, 32);
  sm_config_set_in_shift(&config, false, false, 32);
  float divider = static_cast<float>(clock_get_hz(clk_sys)) /
                  (I2C_FREQUENCY_HZ * PIO_CYCLES_PER_I2C_BIT);
  sm_config_set_clkdiv(&config, divider);

  pio_sm_init(pio, state_machine, offset, &config);
  pio_sm_set_pins_with_mask(
      pio, state_machine, 0,
      (1u << PIN_SCL) | (1u << PIN_SDA));
  pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SCL, 1, false);
  pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SDA, 1, false);
  pio_sm_set_enabled(pio, state_machine, true);
  return true;
}

void mark_failed(DisplayFailure reason) {
  failure = reason;
  if (state_machine >= 0) {
    pio_sm_set_enabled(pio, state_machine, false);
    pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SCL, 1, false);
    pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SDA, 1, false);
    pio_sm_unclaim(pio, state_machine);
    pio_remove_program(pio, &matrix_i2c_program, offset);
    state_machine = -1;
  }
  state = DisplayState::failed;
}

void begin_matrix_transfer() {
  if (state == DisplayState::initialize) {
    if (initialization_index == 2 || initialization_index == 3) {
      const uint8_t blank[I2C_CHUNK_BYTES] = {};
      begin_transfer(display_address,
                     initialization_index == 2 ? 0x00 : 0x08,
                     blank, sizeof(blank));
    } else {
      constexpr uint8_t commands[] = {0x21, 0x80, 0x00, 0x00, 0xe0, 0x81};
      begin_transfer(display_address, commands[initialization_index], nullptr, 0);
    }
    return;
  }

  uint8_t data[I2C_CHUNK_BYTES] = {};
  uint8_t first_row = matrix_chunk * 4;
  if (matrix_row >= first_row && matrix_row < first_row + 4)
    data[(matrix_row - first_row) * 2] = 0xff;
  begin_transfer(display_address, matrix_chunk * I2C_CHUNK_BYTES,
                 data, sizeof(data));
}

void service_display(bool updates_allowed) {
  if (state == DisplayState::failed)
    return;
  if (!updates_allowed && transfer.step == TransferStep::idle)
    return;
  if (state == DisplayState::uninitialized) {
    if (!initialize_bus()) {
      mark_failed(failure);
    } else {
      state = DisplayState::scan_addresses;
    }
    return;
  }
  if (transfer.step == TransferStep::idle) {
    if (state == DisplayState::scan_addresses) {
      begin_transfer(scan_address, 0, nullptr, 0, true);
    } else {
      if (state == DisplayState::ready && time_us_64() < next_matrix_update_us)
        return;
      begin_matrix_transfer();
    }
  }
  TransferResult result = service_transfer();
  if (result == TransferResult::pending)
    return;
  if (state == DisplayState::scan_addresses) {
    if (result == TransferResult::complete) {
      display_address = scan_address;
      state = DisplayState::initialize;
    } else if (result == TransferResult::timeout) {
      mark_failed(DisplayFailure::scan_timeout);
    } else if (++scan_address > LAST_SCANNED_ADDRESS) {
      mark_failed(DisplayFailure::no_device);
    }
    return;
  }
  if (result != TransferResult::complete) {
    mark_failed(state == DisplayState::initialize
                    ? DisplayFailure::initialization_nack
                    : DisplayFailure::ram_nack);
    return;
  }
  if (state == DisplayState::initialize) {
    if (++initialization_index == 6)
      state = DisplayState::ready;
  } else if (++matrix_chunk == 2) {
    matrix_chunk = 0;
    matrix_row = (matrix_row + 1) % 8;
    next_matrix_update_us = time_us_64() + matrix_update_interval_us();
  }
}
}  // namespace

extern "C" void z80_matrix_core1_service(bool updates_allowed) {
  service_display(updates_allowed);
  // One atomic word keeps core-0 status independent of the core-1 driver state.
  uint32_t snapshot = static_cast<uint32_t>(state) |
                      (static_cast<uint32_t>(display_address) << 8) |
                      (static_cast<uint32_t>(failure) << 16) |
                      (static_cast<uint32_t>(pio_timed_out) << 24);
  __atomic_store_n(&published_status, snapshot, __ATOMIC_RELEASE);
}

extern "C" void z80_matrix_set_wifi_connected(bool connected) {
  if (wifi_connected == connected)
    return;
  wifi_connected = connected;
  if (state == DisplayState::ready)
    next_matrix_update_us = time_us_64() + matrix_update_interval_us();
}

extern "C" void z80_matrix_print_status(void) {
  constexpr const char *state_names[] = {
      "uninitialized", "scanning", "initializing", "ready", "failed",
  };
  uint32_t snapshot = __atomic_load_n(&published_status, __ATOMIC_ACQUIRE);
  auto display_state = static_cast<DisplayState>(snapshot & 0xff);
  uint8_t display_address = (snapshot >> 8) & 0xff;
  uint8_t failure = (snapshot >> 16) & 0xff;
  bool timed_out = (snapshot & (1u << 24)) != 0;
  char address[12] = "unavailable";
  if (display_address != 0)
    snprintf(address, sizeof(address), "0x%02x", display_address);
  printf("[matrix] driver=HT16K33 transport=PIO_I2C SCL=GP%u SDA=GP%u "
         "state=%s address=%s brightness=0 pattern=moving_row interval_ms=%llu\n",
         PIN_SCL, PIN_SDA, state_names[static_cast<uint8_t>(display_state)], address,
         static_cast<unsigned long long>(OFFLINE_ROW_INTERVAL_US / 1000));
  if (display_state == DisplayState::failed)
    printf("[matrix] FAIL: %s%s; display service disabled.\n", failure_reasons[failure],
           timed_out ? " (PIO FIFO timeout)" : "");
}
