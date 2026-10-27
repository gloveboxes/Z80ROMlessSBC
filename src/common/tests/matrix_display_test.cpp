#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "hardware/pio.h"
#include "hardware/dma.h"

struct test_pio {
  uint32_t txf[4] = {}, rxf[4] = {};
  pio_sm_config config;
  std::vector<uint16_t> instructions;
  std::deque<uint32_t> received;
  std::vector<std::vector<uint8_t>> transactions;
  std::vector<uint8_t> transaction;
  uint pc = 0;
  uint32_t x = 0, y = 0, osr = 0, isr = 0;
  bool scl = true, sda = true, enabled = false;
  bool stalled = false, acknowledge = true, available = true;
  bool tx_full = false, sm_available = true;
  bool rx_blocked = false;
  uint fifo_operations = 0;
  size_t nack_byte = 0;
  uint8_t device_address = 0x72;
  uint bits = 0;
  uint8_t byte = 0;
  uint64_t cycles = 0, high_since = 0;
  uint64_t minimum_high = UINT64_MAX;

  void pins(bool clock, bool data) {
    if (clock && scl && data != sda) {
      if (!data) {
        transaction.clear();
        bits = 0;
        byte = 0;
      } else {
        transactions.push_back(transaction);
      }
    }
    if (!scl && clock) {
      if (bits < 8) {
        byte = static_cast<uint8_t>((byte << 1) | data);
        ++bits;
        if (bits == 8)
          transaction.push_back(byte);
      } else {
        assert(data);  // Master releases SDA during the ACK clock.
        bits = 0;
        byte = 0;
      }
      high_since = cycles;
    }
    if (scl && !clock && cycles > high_since) {
      uint64_t high = cycles - high_since;
      if (high < minimum_high)
        minimum_high = high;
    }
    scl = clock;
    sda = data;
  }

  void execute(uint32_t word) {
    if (stalled)
      return;
    bool consumed = false;
    for (uint steps = 0; steps < 1000; ++steps) {
      uint16_t instruction = instructions.at(pc);
      uint operation = instruction >> 13;
      uint destination = (instruction >> 5) & 7;
      uint value = instruction & 31;
      uint next_pc = pc == config.wrap ? config.wrap_target : pc + 1;
      bool clock = scl;
      bool data = sda;
      if (instruction & 0x1000)
        clock = !(instruction & 0x0800);
      if (operation == 4 && (instruction & 0x80)) {
        if (consumed)
          return;
        osr = word;
        consumed = true;
      } else if (operation == 3) {
        uint count = value ? value : 32;
        uint32_t output = config.out_right
                              ? osr & ((1u << count) - 1)
                              : osr >> (32 - count);
        osr = config.out_right ? osr >> count : osr << count;
        if (destination == 1)
          x = output;
        else {
          assert(destination == 4);
          data = output == 0;
        }
      } else if (operation == 7) {
        if (destination == 1)
          x = value;
        else if (destination == 2)
          y = value;
        else {
          assert(destination == 4);
          data = value == 0;
        }
      } else if (operation == 0) {
        bool jump = destination == 0 ||
                    (destination == 1 && x == 0) ||
                    (destination == 2 && x-- != 0) ||
                    (destination == 5 && x != y);
        if (jump)
          next_pc = value;
      } else if (operation == 5) {
        if (destination == 6) {
          assert(value == 3);
          isr = 0;
        } else {
          assert(destination == 2 && value == 2);
        }
      } else if (operation == 2) {
        assert(destination == 0 && value == 1);
        bool ack = acknowledge && transaction.size() != nack_byte &&
                   !transaction.empty() &&
                   transaction.front() == device_address * 2;
        uint32_t input = ack ? 0 : 1;
        isr = config.in_right ? (isr >> 1) | (input << 31)
                             : (isr << 1) | input;
      } else {
        assert(operation == 4);
        received.push_back(isr);
        isr = 0;
      }
      pins(clock, data);
      cycles += 1 + ((instruction >> 8) & 7);
      pc = next_pc;
    }
    assert(false && "PIO program did not return to its FIFO wait");
  }
};

test_pio machine;
PIO pio0 = &machine;
static uint64_t now;

bool pio_can_add_program(PIO p, const pio_program *) { return p->available; }
int pio_claim_unused_sm(PIO p, bool) { return p->sm_available ? 0 : -1; }
uint pio_add_program(PIO p, const pio_program *program) {
  p->instructions.assign(program->instructions,
                         program->instructions + program->length);
  return 0;
}
void pio_gpio_init(PIO, uint) {}
void pio_sm_init(PIO p, uint, uint pc, const pio_sm_config *config) {
  p->config = *config;
  p->pc = pc;
}
void pio_sm_set_pins_with_mask(PIO, uint, uint32_t value, uint32_t mask) {
  assert(value == 0 && mask == ((1u << 22) | (1u << 26)));
}
void pio_sm_set_consecutive_pindirs(PIO p, uint, uint pin, uint count,
                                   bool output) {
  assert(count == 1);
  if (pin == p->config.scl)
    p->scl = !output;
  else {
    assert(pin == p->config.sda);
    p->sda = !output;
  }
}
void pio_sm_set_enabled(PIO p, uint, bool enabled) { p->enabled = enabled; }
bool pio_sm_is_tx_fifo_full(PIO p, uint) { return p->tx_full; }
bool pio_sm_is_rx_fifo_empty(PIO p, uint) {
  return p->rx_blocked || p->received.empty();
}
void pio_sm_put(PIO p, uint, uint32_t word) {
  assert(p->enabled);
  ++p->fifo_operations;
  p->execute(word);
}
uint32_t pio_sm_get(PIO p, uint) {
  ++p->fifo_operations;
  uint32_t result = p->received.front();
  p->received.pop_front();
  return result;
}
void pio_sm_unclaim(PIO, uint) {}
void pio_remove_program(PIO, const pio_program *, uint) {}
uint32_t clock_get_hz(uint) { return 120000000; }
uint64_t time_us_64() { return now++; }
void tight_loop_contents() { assert(false && "Display must not busy-wait"); }

struct FakeDMA {
  bool claimed = false, busy = false;
  volatile void *write = nullptr;
  const volatile void *read = nullptr;
  uint count = 0;
  dma_channel_config config;
} channels[2];
static bool dma_available = true;
int dma_claim_unused_channel(bool) {
  for (int i = 0; i < 2 && dma_available; ++i)
    if (!channels[i].claimed) { channels[i].claimed = true; return i; }
  return -1;
}
void dma_channel_abort(uint c) { channels[c].busy = false; }
void dma_channel_unclaim(uint c) { channels[c].claimed = false; }
bool dma_channel_is_busy(uint c) { return channels[c].busy; }
uint pio_get_dreq(PIO, uint, bool tx) { return tx ? 1 : 2; }
void dma_channel_configure(uint c, const dma_channel_config *config,
                           volatile void *write, const volatile void *read,
                           uint count, bool start) {
  channels[c].config = *config;
  channels[c].write = write; channels[c].read = read;
  channels[c].count = count; channels[c].busy = start;
}
static void run_dma(bool stop_result = true) {
  auto &tx = channels[0]; auto &rx = channels[1];
  assert(tx.config.dreq == 1 && rx.config.dreq == 2);
  assert(tx.config.read_increment && !tx.config.write_increment);
  assert(!rx.config.read_increment && rx.config.write_increment);
  assert(tx.count == 20 && rx.count == 19);
  assert(tx.write == &machine.txf[0] && rx.read == &machine.rxf[0]);
  auto words = static_cast<const volatile uint32_t *>(tx.read);
  auto results = static_cast<volatile uint32_t *>(rx.write);
  uint result = 0;
  for (uint i = 0; i < tx.count; ++i) {
    machine.execute(words[i]);
    while (!machine.received.empty()) {
      results[result++] = machine.received.front();
      machine.received.pop_front();
    }
  }
  assert(result == 19);
  tx.busy = false;
  rx.busy = !stop_result;
}

static std::string diagnostic_output;
static int display_printf(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  assert(length >= 0 && static_cast<size_t>(length) < sizeof(buffer));
  diagnostic_output += buffer;
  return length;
}

#define printf display_printf
#include "../../stage10_websocket_terminal/matrix_display.cpp"
#undef printf

static void reset() {
  machine = {};
  state = DisplayState::uninitialized;
  state_machine = -1;
  scan_address = FIRST_SCANNED_ADDRESS;
  initialization_index = matrix_row = 0;
  tx_dma = rx_dma = -1;
  frame_active = false;
  channels[0] = channels[1] = {};
  dma_available = true;
  pio_timed_out = false;
  wifi_connected = frame_wifi_connected = displayed_wifi_connected = false;
  failure = DisplayFailure::none;
  published_status = 0;
  display_address = 0;
  next_matrix_update_us = now = 0;
  transfer = {};
  diagnostic_output.clear();
}

static void service_once(bool updates_allowed = true) {
  uint64_t before = now;
  uint operations = machine.fifo_operations;
  z80_matrix_core1_service(updates_allowed);
  assert(now - before <= 3);
  assert(machine.fifo_operations - operations <= 1);
}

static void service_until(DisplayState expected) {
  for (uint i = 0; i < 2000 && state != expected; ++i)
    service_once();
  assert(state == expected);
}

static void assert_disabled() {
  assert(state == DisplayState::failed);
  assert(!machine.enabled && machine.scl && machine.sda);
  assert(diagnostic_output.empty());
  auto before_count = machine.transactions.size();
  service_once();
  assert(machine.transactions.size() == before_count);
  z80_matrix_print_status();
  assert(diagnostic_output.find("state=failed") != std::string::npos);
  assert(diagnostic_output.find(failure_reasons[static_cast<uint8_t>(failure)]) !=
         std::string::npos);
  assert(diagnostic_output.find("[matrix] FAIL:") != std::string::npos);
  if (pio_timed_out)
    assert(diagnostic_output.find("PIO FIFO timeout") != std::string::npos);
}

int main() {
  reset();
  service_once(false);
  assert(state == DisplayState::uninitialized);
  service_until(DisplayState::ready);
  assert(display_address == 0x72 && machine.transactions.size() == 9);
  for (uint row = 0; row < 16; ++row) {
    auto before = machine.transactions.size();
    service_once();
    assert(frame_active && machine.transactions.size() == before);
    auto deadline = next_matrix_update_us;
    for (uint i = 0; i < 10; ++i) service_once();
    assert(channels[0].busy && channels[1].busy);
    run_dma();
    std::vector<uint8_t> expected(18, 0);
    expected[0] = 0xe4;
    expected[2 + (row % 8) * 2] = 0xff;
    assert(machine.transactions.back() == expected);
    now = deadline;
    service_once(false);
    assert(!frame_active && matrix_row == (row + 1) % 8);
    service_once(false);
    assert(machine.transactions.size() == before + 1);
  }
  z80_matrix_set_wifi_connected(true);
  service_once();
  z80_matrix_set_wifi_connected(false); // In-flight buffer remains unchanged.
  run_dma();
  for (uint row = 0; row < 8; ++row)
    assert(machine.transactions.back()[2 + row * 2] == 1);
  now = next_matrix_update_us;
  service_once(false);
  assert(displayed_wifi_connected);
  assert(machine.minimum_high * machine.config.divider / 120 >= 4.0);

  for (uint byte = 1; byte <= 18; ++byte) {
    reset(); service_until(DisplayState::ready);
    machine.nack_byte = byte;
    service_once(); run_dma();
    assert(machine.transactions.back().size() == 18);
    now = next_matrix_update_us; service_once();
    assert_disabled();
    assert(!channels[0].claimed && !channels[1].claimed);
  }
  reset(); service_until(DisplayState::ready);
  service_once(); run_dma(false); // TX done, STOP result not received.
  now = next_matrix_update_us; service_once(false);
  assert(pio_timed_out); assert_disabled();

  reset(); service_until(DisplayState::ready);
  service_once();
  // A failed DMA can become idle before writing all results.
  channels[0].busy = channels[1].busy = false;
  now = next_matrix_update_us; service_once();
  assert(pio_timed_out); assert_disabled();

  reset(); service_until(DisplayState::ready);
  channels[0].claimed = true; // Only one channel available; release partial claim.
  service_once(); assert_disabled();
  assert(!channels[1].claimed);

  for (uint8_t address : {0x70, 0x77}) {
    reset(); machine.device_address = address;
    service_until(DisplayState::ready);
    assert(display_address == address);
  }
  reset(); service_until(DisplayState::ready);
  z80_matrix_set_wifi_connected(true);
  for (uint position = 0; position < 8; ++position) {
    service_once(); run_dma();
    for (uint row = 0; row < 8; ++row)
      assert(machine.transactions.back()[2 + row * 2] == (1u << position));
    now = next_matrix_update_us; service_once(false);
  }

  reset(); service_until(DisplayState::ready);
  dma_available = false; service_once(); assert_disabled();
  reset(); machine.acknowledge = false;
  service_until(DisplayState::failed); assert_disabled();
  reset(); service_until(DisplayState::initialize);
  machine.nack_byte = 2;
  service_until(DisplayState::failed); assert_disabled();
  reset(); service_once(); machine.stalled = true;
  service_once(); service_once(); service_once();
  now += PIO_TIMEOUT_US;
  service_once(); assert_disabled();
  reset(); machine.available = false; service_once(); assert_disabled();
  reset(); machine.sm_available = false; service_once(); assert_disabled();
  puts("PASS: assembled PIO, DMA frames, ACK collection, STOP completion, gating and failure cleanup");
}
