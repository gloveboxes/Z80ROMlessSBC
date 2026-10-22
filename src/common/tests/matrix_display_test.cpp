#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "hardware/pio.h"

struct test_pio {
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
        bool ack = acknowledge && !transaction.empty() &&
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
bool pio_sm_is_rx_fifo_empty(PIO p, uint) { return p->received.empty(); }
void pio_sm_put(PIO p, uint, uint32_t word) {
  assert(p->enabled);
  p->execute(word);
}
uint32_t pio_sm_get(PIO p, uint) {
  uint32_t result = p->received.front();
  p->received.pop_front();
  return result;
}
void pio_sm_unclaim(PIO, uint) {}
void pio_remove_program(PIO, const pio_program *, uint) {}
uint32_t clock_get_hz(uint) { return 120000000; }
uint64_t time_us_64() { return now++; }
void tight_loop_contents() {}

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
  initialization_index = matrix_row = matrix_chunk = 0;
  pio_timed_out = false;
  failure = DisplayFailure::none;
  published_status = 0;
  display_address = 0;
  next_matrix_update_us = now = 0;
  diagnostic_output.clear();
}

static void assert_disabled() {
  assert(state == DisplayState::failed);
  assert(!machine.enabled && machine.scl && machine.sda);
  assert(diagnostic_output.empty());
  auto before_count = machine.transactions.size();
  z80_matrix_core1_service();
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
  z80_matrix_print_status();
  assert(diagnostic_output.find("state=uninitialized address=unavailable") !=
         std::string::npos);
  diagnostic_output.clear();
  for (uint i = 0; i < 10; ++i)
    z80_matrix_core1_service();
  assert(state == DisplayState::ready && display_address == 0x72);
  assert(machine.config.scl == 26 && machine.config.sda == 22);
  assert(!machine.config.out_right && !machine.config.in_right);
  assert(diagnostic_output.empty());
  z80_matrix_print_status();
  assert(diagnostic_output.find(
      "[matrix] driver=HT16K33 transport=PIO_I2C SCL=GP26 SDA=GP22 "
      "state=ready address=0x72 brightness=0 pattern=moving_row interval_ms=500") !=
      std::string::npos);
  assert(diagnostic_output.find("FAIL") == std::string::npos);
  auto ready_status = diagnostic_output;
  state = DisplayState::failed;
  display_address = 0x77;
  diagnostic_output.clear();
  z80_matrix_print_status();
  assert(diagnostic_output == ready_status);
  state = DisplayState::ready;
  display_address = 0x72;
  diagnostic_output.clear();
  const uint8_t address_byte = 0x72 * 2;
  assert(machine.transactions.size() == 9);
  for (uint i = 0; i < 3; ++i)
    assert(machine.transactions[i] == std::vector<uint8_t>{
        static_cast<uint8_t>((0x70 + i) * 2)});
  assert(machine.transactions[3] == (std::vector<uint8_t>{address_byte, 0x21}));
  assert(machine.transactions[4] == (std::vector<uint8_t>{address_byte, 0x80}));
  for (uint chunk = 0; chunk < 2; ++chunk) {
    std::vector<uint8_t> expected(10, 0);
    expected[0] = address_byte;
    expected[1] = chunk * 8;
    assert(machine.transactions[5 + chunk] == expected);
  }
  assert(machine.transactions[7] == (std::vector<uint8_t>{address_byte, 0xe0}));
  assert(machine.transactions[8] == (std::vector<uint8_t>{address_byte, 0x81}));
  for (uint row = 0; row < 16; ++row) {
    now = next_matrix_update_us;
    for (uint chunk = 0; chunk < 2; ++chunk) {
      auto before_count = machine.transactions.size();
      z80_matrix_core1_service();
      assert(machine.transactions.size() == before_count + 1);
      std::vector<uint8_t> expected(10, 0);
      expected[0] = address_byte;
      expected[1] = chunk * 8;
      uint active_row = row % 8;
      if (active_row / 4 == chunk)
        expected[2 + (active_row % 4) * 2] = 0xff;
      assert(machine.transactions.back() == expected);
    }
    assert(next_matrix_update_us >= now + 499'999);
    auto before_count = machine.transactions.size();
    for (uint i = 0; i < 10; ++i)
      z80_matrix_core1_service();
    now = next_matrix_update_us - 1;
    z80_matrix_core1_service();
    assert(machine.transactions.size() == before_count);
  }
  assert(machine.minimum_high * machine.config.divider / 120 >= 4.0);
  assert(diagnostic_output.empty());

  for (uint8_t address : {0x70, 0x77}) {
    reset();
    machine.device_address = address;
    for (uint i = 0; i < 20; ++i)
      z80_matrix_core1_service();
    assert(state == DisplayState::ready && display_address == address);
  }

  reset();
  for (uint i = 0; i < 4; ++i)
    z80_matrix_core1_service();
  assert(state == DisplayState::initialize);
  machine.acknowledge = false;
  z80_matrix_core1_service();
  assert_disabled();

  reset();
  for (uint i = 0; i < 10; ++i)
    z80_matrix_core1_service();
  machine.acknowledge = false;
  z80_matrix_core1_service();
  assert_disabled();

  reset();
  machine.acknowledge = false;
  for (uint i = 0; i < 10; ++i)
    z80_matrix_core1_service();
  assert(machine.transactions.size() == 8);
  assert_disabled();

  reset();
  z80_matrix_core1_service();
  machine.tx_full = true;
  uint64_t before = now;
  z80_matrix_core1_service();
  assert(pio_timed_out && now - before < 2 * PIO_TIMEOUT_US);
  assert_disabled();

  reset();
  z80_matrix_core1_service();
  machine.stalled = true;
  before = now;
  z80_matrix_core1_service();
  assert(pio_timed_out && now - before < 2 * PIO_TIMEOUT_US);
  assert_disabled();

  reset();
  machine.available = false;
  z80_matrix_core1_service();
  assert_disabled();

  reset();
  machine.sm_available = false;
  z80_matrix_core1_service();
  assert_disabled();
  puts("PASS: assembled matrix PIO, HT16K33 initialization, row pattern, "
       "timing, ACK/NACK, missing display and timeout isolation");
}
