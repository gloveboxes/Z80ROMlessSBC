#include "display_i2c.h"

#include <cassert>
#include <initializer_list>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "display_i2c.pio.h"
#include "pico/stdlib.h"
#include "z80sbc/ram_func.h"
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
uint32_t deadline;
uint8_t *read_data;

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
  // 12 PIO cycles per bit.
  sm_config_set_clkdiv(&config,
                       static_cast<float>(clock_get_hz(clk_sys)) / (BAUD * 12));
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
  // Everything that does not change between transfers is written once. A
  // transfer then needs only its counts and the two start addresses.
  auto rx = dma_channel_get_default_config(rx_dma);
  channel_config_set_transfer_data_size(&rx, DMA_SIZE_32);
  channel_config_set_read_increment(&rx, false);
  channel_config_set_write_increment(&rx, true);
  channel_config_set_dreq(&rx, pio_get_dreq(pio, state_machine, false));
  dma_channel_set_config(rx_dma, &rx, false);
  dma_channel_set_read_addr(rx_dma, &pio->rxf[state_machine], false);
  auto tx = dma_channel_get_default_config(tx_dma);
  channel_config_set_transfer_data_size(&tx, DMA_SIZE_32);
  channel_config_set_read_increment(&tx, true);
  channel_config_set_write_increment(&tx, false);
  channel_config_set_dreq(&tx, pio_get_dreq(pio, state_machine, true));
  dma_channel_set_config(tx_dma, &tx, false);
  dma_channel_set_write_addr(tx_dma, &pio->txf[state_machine], false);
  return true;
}

uint32_t encode(uint8_t byte) {
  return static_cast<uint32_t>(static_cast<uint8_t>(~byte)) << 22;
}
}

static bool __not_in_flash_func(start)(uint8_t address, const uint8_t *data,
                                       uint8_t *read, size_t length) {
  assert(address < 0x80 && length <= MAX_DATA_BYTES && (data || length == 0));
  if (bus_failure != Failure::none || active)
    return false;
  if (state_machine < 0 && !initialize())
    return false;
  read_data = read;
  uint32_t *word = commands;
  *word++ = 1u << 30;
  *word++ = encode(static_cast<uint8_t>((address << 1) | (read ? 1 : 0)));
  if (read) {
    // ACK every byte but the last.
    for (size_t i = 1; i < length; ++i)
      *word++ = 1u << 21;
    if (length)
      *word++ = 0;
  } else {
    for (size_t i = 0; i < length; ++i)
      *word++ = encode(data[i]);
  }
  *word = 2u << 30;
  result_count = length + 2;
  // DMA writes every other result before RX goes idle; the last slot is the
  // STOP result, so it alone needs a sentinel to certify completion.
  results[result_count - 1] = UINT32_MAX;
  active = true;
  deadline = z80_time_us_32() + 5000 + (length + 1) * 120;
  dma_channel_set_transfer_count(rx_dma, result_count, false);
  dma_channel_set_write_addr(rx_dma, results, true);
  dma_channel_set_transfer_count(tx_dma, length + 3, false);
  dma_channel_set_read_addr(tx_dma, commands, true);
  return true;
}

bool __not_in_flash_func(begin)(uint8_t address, const uint8_t *data, size_t length) {
  return start(address, data, nullptr, length);
}

bool __not_in_flash_func(begin_read)(uint8_t address, uint8_t *data, size_t length) {
  assert(data && length);
  return start(address, data, data, length);
}

bool __not_in_flash_func(busy)() { return active; }

Result __not_in_flash_func(poll)() {
  if (bus_failure != Failure::none)
    return Result::failed;
  if (!active)
    return Result::pending;
  // RX finishes last (it collects the STOP result), and TX has been consumed
  // by then, so RX alone says whether the transfer is still running.
  if (dma_channel_is_busy(rx_dma)) {
    if (static_cast<int32_t>(z80_time_us_32() - deadline) < 0)
      return Result::pending;
    fail(Failure::timeout);
    return Result::failed;
  }
  // RX completion includes STOP; idle channels alone cannot certify a transfer.
  if (results[result_count - 1] != 0) {
    fail(Failure::timeout);
    return Result::failed;
  }
  // Every genuine result is the nine sampled bits; anything above is a slot
  // that was never written.
  uint32_t seen = 0;
  for (size_t i = 0; i + 1 < result_count; ++i)
    seen |= results[i];
  if (seen >> 9) {
    fail(Failure::timeout);
    return Result::failed;
  }
  bool acknowledged;
  if (read_data) {
    acknowledged = (results[0] & 1u) == 0;
    for (size_t i = 1; i + 1 < result_count; ++i)
      read_data[i - 1] = static_cast<uint8_t>(results[i] >> 1);
  } else {
    acknowledged = (seen & 1u) == 0;
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
