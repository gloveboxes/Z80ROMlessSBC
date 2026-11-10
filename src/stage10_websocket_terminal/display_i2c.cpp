#include "display_i2c.h"

#include <cassert>
#include <initializer_list>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "display_i2c.pio.h"
#include "pico/stdlib.h"
#include "z80sbc/ram_timer.h"

namespace display_i2c {
static_assert(PIN_SCL == 26, "PIO clock-stretch WAIT instructions use GPIO 26");
namespace {
PIO pio = pio0;
int state_machine = -1;
uint offset;
int tx_dma = -1, rx_dma = -1;
Failure bus_failure = Failure::none;
bool active;
uint32_t commands[MAX_DATA_BYTES + 3];
volatile uint32_t results[MAX_DATA_BYTES + 2];
size_t result_count;
uint64_t deadline;
uint8_t *read_data;
uint32_t current_baud;

void fail(Failure reason) {
  bus_failure = reason;
  for (int *channel : {&tx_dma, &rx_dma}) {
    if (*channel >= 0) {
      dma_channel_abort(*channel);
      dma_channel_unclaim(*channel);
      *channel = -1;
    }
  }
  if (state_machine >= 0) {
    pio_sm_set_enabled(pio, state_machine, false);
    pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SCL, 1, false);
    pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SDA, 1, false);
    pio_sm_unclaim(pio, state_machine);
    pio_remove_program(pio, &display_i2c_program, offset);
    state_machine = -1;
  }
  active = false;
}

bool initialize() {
  if (!pio_can_add_program(pio, &display_i2c_program)) {
    fail(Failure::program);
    return false;
  }
  state_machine = pio_claim_unused_sm(pio, false);
  if (state_machine < 0) {
    fail(Failure::state_machine);
    return false;
  }
  offset = pio_add_program(pio, &display_i2c_program);
  pio_gpio_init(pio, PIN_SCL);
  pio_gpio_init(pio, PIN_SDA);
  auto config = display_i2c_program_get_default_config(offset);
  sm_config_set_sideset_pins(&config, PIN_SCL);
  sm_config_set_out_pins(&config, PIN_SDA, 1);
  sm_config_set_set_pins(&config, PIN_SDA, 1);
  sm_config_set_in_pins(&config, PIN_SDA);
  sm_config_set_out_shift(&config, false, false, 32);
  sm_config_set_in_shift(&config, false, false, 32);
  current_baud = 400000;
  sm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) /
                                 (current_baud * 12));
  pio_sm_init(pio, state_machine, offset, &config);
  pio_sm_set_pins_with_mask(pio, state_machine, 0,
                           (1u << PIN_SCL) | (1u << PIN_SDA));
  pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SCL, 1, false);
  pio_sm_set_consecutive_pindirs(pio, state_machine, PIN_SDA, 1, false);
  pio_sm_set_enabled(pio, state_machine, true);
  tx_dma = dma_claim_unused_channel(false);
  rx_dma = dma_claim_unused_channel(false);
  if (tx_dma < 0 || rx_dma < 0) {
    fail(Failure::dma);
    return false;
  }
  return true;
}

uint32_t encode(uint8_t byte) {
  return static_cast<uint32_t>(static_cast<uint8_t>(~byte)) << 22;
}
}

static bool start(uint8_t address, const uint8_t *data, uint8_t *read,
                  size_t length, uint32_t baud) {
  assert(address < 0x80 && length <= MAX_DATA_BYTES && (data || length == 0));
  assert(baud == 100000 || baud == 400000);
  if (bus_failure != Failure::none || active)
    return false;
  if (state_machine < 0 && !initialize())
    return false;
  if (baud != current_baud) {
    pio_sm_set_clkdiv(pio, state_machine,
                     static_cast<float>(clock_get_hz(clk_sys)) / (baud * 12));
    current_baud = baud;
  }
  read_data = read;
  commands[0] = 1u << 30;
  commands[1] = encode(static_cast<uint8_t>((address << 1) | (read ? 1 : 0)));
  for (size_t i = 0; i < length; ++i)
    commands[i + 2] = read ? (i + 1 == length ? 0 : 1u << 21) : encode(data[i]);
  commands[length + 2] = 2u << 30;
  result_count = length + 2;
  for (size_t i = 0; i < result_count; ++i)
    results[i] = UINT32_MAX;
  active = true;
  deadline = z80_time_us_64() + 5000 + (length + 1) * (baud == 100000 ? 120 : 30);
  auto rx = dma_channel_get_default_config(rx_dma);
  channel_config_set_transfer_data_size(&rx, DMA_SIZE_32);
  channel_config_set_read_increment(&rx, false);
  channel_config_set_write_increment(&rx, true);
  channel_config_set_dreq(&rx, pio_get_dreq(pio, state_machine, false));
  dma_channel_configure(rx_dma, &rx, results, &pio->rxf[state_machine],
                        result_count, true);
  auto tx = dma_channel_get_default_config(tx_dma);
  channel_config_set_transfer_data_size(&tx, DMA_SIZE_32);
  channel_config_set_read_increment(&tx, true);
  channel_config_set_write_increment(&tx, false);
  channel_config_set_dreq(&tx, pio_get_dreq(pio, state_machine, true));
  dma_channel_configure(tx_dma, &tx, &pio->txf[state_machine], commands,
                        length + 3, true);
  return true;
}

bool begin(uint8_t address, const uint8_t *data, size_t length, uint32_t baud) {
  return start(address, data, nullptr, length, baud);
}

bool begin_read(uint8_t address, uint8_t *data, size_t length, uint32_t baud) {
  assert(data && length);
  return start(address, data, data, length, baud);
}

bool busy() { return active; }

Result poll() {
  if (bus_failure != Failure::none)
    return Result::failed;
  if (!active)
    return Result::pending;
  // RX finishes last (it collects the STOP result), so test it first.
  if (dma_channel_is_busy(rx_dma) || dma_channel_is_busy(tx_dma)) {
    if (z80_time_us_64() < deadline)
      return Result::pending;
    fail(Failure::timeout);
    return Result::failed;
  }
  // RX completion includes STOP; idle channels alone cannot certify a transfer.
  if (results[result_count - 1] != 0) {
    fail(Failure::timeout);
    return Result::failed;
  }
  bool acknowledged = true;
  for (size_t i = 0; i + 1 < result_count; ++i) {
    uint32_t result = results[i];
    if (result == UINT32_MAX) {
      fail(Failure::timeout);
      return Result::failed;
    }
    if (!read_data || i == 0)
      acknowledged &= (result & 1u) == 0;
    else
      read_data[i - 1] = static_cast<uint8_t>(result >> 1);
  }
  active = false;
  return acknowledged ? Result::complete : Result::nack;
}

Failure failure() { return bus_failure; }

const char *failure_reason(Failure reason) {
  constexpr const char *reasons[] = {
      "none", "no PIO instruction memory for I2C program",
      "no PIO state machine available", "no DMA channels available",
      "PIO/DMA transfer timeout or missing STOP/ACK result",
  };
  return reasons[static_cast<unsigned>(reason)];
}
}
