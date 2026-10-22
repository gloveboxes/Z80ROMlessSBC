#include <cstdint>
#include <cstdio>

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
constexpr uint64_t ROW_INTERVAL_US = 500'000;
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

bool write_word(uint32_t value, bool read_result, uint32_t *result = nullptr) {
  uint64_t deadline = time_us_64() + PIO_TIMEOUT_US;
  while (pio_sm_is_tx_fifo_full(pio, state_machine)) {
    if (time_us_64() >= deadline) {
      pio_timed_out = true;
      return false;
    }
    tight_loop_contents();
  }
  pio_sm_put(pio, state_machine, value);
  if (!read_result)
    return true;
  while (pio_sm_is_rx_fifo_empty(pio, state_machine)) {
    if (time_us_64() >= deadline) {
      pio_timed_out = true;
      return false;
    }
    tight_loop_contents();
  }
  if (result != nullptr)
    *result = pio_sm_get(pio, state_machine);
  else
    (void)pio_sm_get(pio, state_machine);
  return true;
}

bool start_action(uint8_t action) {
  return write_word(static_cast<uint32_t>(action) << 30, false);
}

bool write_byte(uint8_t value) {
  uint32_t result = 1;
  // Two action bits precede eight inverted, MSB-first pin-direction bits.
  uint32_t encoded =
      static_cast<uint32_t>(static_cast<uint8_t>(~value)) << 22;
  if (!write_word(encoded, true, &result))
    return false;
  return (result & 1u) == 0;
}

bool probe_address(uint8_t address) {
  if (!start_action(1))
    return false;
  bool acknowledged = write_byte(static_cast<uint8_t>(address << 1));
  bool stopped = start_action(2);
  return acknowledged && stopped;
}

bool write_transaction(uint8_t command, const uint8_t *data, size_t length) {
  if (!start_action(1))
    return false;
  bool acknowledged = write_byte(static_cast<uint8_t>(display_address << 1));
  if (acknowledged)
    acknowledged = write_byte(command);
  for (size_t i = 0; acknowledged && i < length; ++i)
    acknowledged = write_byte(data[i]);
  bool stopped = start_action(2);
  return acknowledged && stopped;
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

void service_matrix() {
  if (state == DisplayState::initialize) {
    bool acknowledged;
    if (initialization_index == 2 || initialization_index == 3) {
      const uint8_t blank[I2C_CHUNK_BYTES] = {};
      acknowledged = write_transaction(
          initialization_index == 2 ? 0x00 : 0x08, blank, sizeof(blank));
    } else {
      constexpr uint8_t commands[] = {0x21, 0x80, 0x00, 0x00, 0xe0, 0x81};
      acknowledged = write_transaction(commands[initialization_index], nullptr, 0);
    }
    if (!acknowledged) {
      mark_failed(DisplayFailure::initialization_nack);
      return;
    }
    if (++initialization_index == 6) {
      state = DisplayState::ready;
    }
    return;
  }

  if (time_us_64() < next_matrix_update_us)
    return;
  uint8_t data[I2C_CHUNK_BYTES] = {};
  uint8_t first_row = matrix_chunk * 4;
  if (matrix_row >= first_row && matrix_row < first_row + 4)
    data[(matrix_row - first_row) * 2] = 0xff;
  if (!write_transaction(matrix_chunk * I2C_CHUNK_BYTES, data, sizeof(data))) {
    mark_failed(DisplayFailure::ram_nack);
    return;
  }
  if (++matrix_chunk == 2) {
    matrix_chunk = 0;
    matrix_row = (matrix_row + 1) % 8;
    next_matrix_update_us = time_us_64() + ROW_INTERVAL_US;
  }
}
void service_display() {
  if (state == DisplayState::failed)
    return;
  if (state == DisplayState::uninitialized) {
    if (!initialize_bus()) {
      mark_failed(failure);
    } else {
      state = DisplayState::scan_addresses;
    }
    return;
  }
  if (state == DisplayState::scan_addresses) {
    if (probe_address(scan_address)) {
      display_address = scan_address;
      state = DisplayState::initialize;
    } else if (pio_timed_out) {
      mark_failed(DisplayFailure::scan_timeout);
    } else if (++scan_address > LAST_SCANNED_ADDRESS) {
      mark_failed(DisplayFailure::no_device);
    }
    return;
  }
  service_matrix();
}
}  // namespace

extern "C" void z80_matrix_core1_service(void) {
  service_display();
  // One atomic word keeps core-0 status independent of the core-1 driver state.
  uint32_t snapshot = static_cast<uint32_t>(state) |
                      (static_cast<uint32_t>(display_address) << 8) |
                      (static_cast<uint32_t>(failure) << 16) |
                      (static_cast<uint32_t>(pio_timed_out) << 24);
  __atomic_store_n(&published_status, snapshot, __ATOMIC_RELEASE);
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
         static_cast<unsigned long long>(ROW_INTERVAL_US / 1000));
  if (display_state == DisplayState::failed)
    printf("[matrix] FAIL: %s%s; display service disabled.\n", failure_reasons[failure],
           timed_out ? " (PIO FIFO timeout)" : "");
}
