#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
#include <cstring>

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "../io_service.h"

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
  uint8_t device_address = 0x3c;  // Zero models an absent device.
  uint bits = 0;
  uint8_t byte = 0;
  uint byte_index = 0;
  bool sampled_input = true;
  std::vector<uint8_t> read_bytes;
  std::vector<bool> read_acks;
  uint stretch_cycles = 0;
  uint64_t cycles = 0, high_since = 0, low_since = 0, stop_since = 0;
  bool stopped = false;
  uint64_t minimum_high = UINT64_MAX;
  uint64_t minimum_low = UINT64_MAX, minimum_bus_free = UINT64_MAX;
  uint64_t minimum_bit_period = UINT64_MAX, maximum_bit_period = 0;

  void pins(bool clock, bool data) {
    if (clock && scl && data != sda) {
      if (!data) {
        if (stopped)
          minimum_bus_free = std::min(minimum_bus_free, cycles - stop_since);
        transaction.clear();
        bits = 0;
        byte = 0;
        byte_index = 0;
      } else {
        transactions.push_back(transaction);
        stopped = true;
        stop_since = cycles;
      }
    }
    if (!scl && clock) {
      minimum_low = std::min(minimum_low, cycles - low_since);
      if (bits > 0 && bits < 8) {
        minimum_bit_period = std::min(minimum_bit_period, cycles - high_since);
        maximum_bit_period = std::max(maximum_bit_period, cycles - high_since);
      }
      if (bits < 8) {
        sampled_input = data;
        if (byte_index && (transaction.front() & 1) && data) {
          assert(byte_index <= read_bytes.size());
          sampled_input = (read_bytes[byte_index - 1] >> (7 - bits)) & 1;
        }
        byte = static_cast<uint8_t>((byte << 1) | sampled_input);
        ++bits;
        if (bits == 8)
          transaction.push_back(byte);
      } else {
        if (byte_index && (transaction.front() & 1)) {
          read_acks.push_back(!data);
          sampled_input = data;
        } else {
          assert(data);  // Master releases SDA during the write ACK clock.
          sampled_input = !(acknowledge && transaction.size() != nack_byte &&
                            device_address &&
                            (transaction.front() >> 1) == device_address);
        }
        bits = 0;
        byte = 0;
        ++byte_index;
      }
      high_since = cycles;
    }
    if (scl && !clock && cycles > high_since) {
      low_since = cycles;
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
      } else if (operation == 1) {
        assert(destination == 4 && value == 26);
        cycles += stretch_cycles;
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
        uint32_t input = sampled_input;
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
void pio_sm_set_clkdiv(PIO p, uint, float divider) { p->config.divider = divider; }
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
void dma_channel_set_config(uint c, const dma_channel_config *config, bool start) {
  assert(!start && !channels[c].busy);
  channels[c].config = *config;
}
void dma_channel_set_transfer_count(uint c, uint32_t count, bool start) {
  assert(!start && !channels[c].busy);
  channels[c].count = count;
}
void dma_channel_set_read_addr(uint c, const volatile void *read, bool start) {
  assert(!channels[c].busy);
  channels[c].read = read;
  if (start) channels[c].busy = true;
}
void dma_channel_set_write_addr(uint c, volatile void *write, bool start) {
  assert(!channels[c].busy);
  channels[c].write = write;
  if (start) channels[c].busy = true;
}
static void run_dma(bool stop_result = true) {
  auto &tx = channels[0]; auto &rx = channels[1];
  assert(tx.config.dreq == 1 && rx.config.dreq == 2);
  assert(tx.config.read_increment && !tx.config.write_increment);
  assert(!rx.config.read_increment && rx.config.write_increment);
  assert(tx.count == rx.count + 1);
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
  assert(result == rx.count);
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
#include "../../stage10_websocket_terminal/display_i2c.cpp"
#include "../../stage10_websocket_terminal/oled_display.cpp"
#undef printf

static char fake_ip[16] = "192.168.1.123";
static unsigned address_reads;
static uint32_t address_generation;
// Mirrors the platform: publishing an address bumps the change counter.
static void set_ip(const char *text) {
  snprintf(fake_ip, sizeof(fake_ip), "%s", text);
  ++address_generation;
}
uint32_t z80_io_ports_network_address_generation(void) { return address_generation; }
static bool browser_connected;
static unsigned browser_reads;
bool z80_terminal_browser_connected(void) {
  ++browser_reads;
  return browser_connected;
}
uint64_t z80_service_millis(void) { return now / 1000; }
uint64_t z80_service_micros(void) { return now; }
size_t z80_io_ports_get_network_address(char *text, size_t capacity) {
  ++address_reads;
  int length = snprintf(text, capacity, "%s", fake_ip);
  return length > 0 && static_cast<size_t>(length) < capacity ? length : 0;
}

static void reset() {
  machine = {};
  display_i2c::state_machine = -1;
  display_i2c::tx_dma = display_i2c::rx_dma = -1;
  display_i2c::bus_failure = display_i2c::Failure::none;
  display_i2c::active = false;
  channels[0] = channels[1] = {};
  dma_available = true;
  now = 0;
  diagnostic_output.clear();
  oled_display::state = oled_display::State::uninitialized;
  oled_display::failure = oled_display::Failure::none;
  oled_display::address = 0x3c;
  oled_display::transfer_active = oled_display::enabled = false;
  memset(oled_display::framebuffer, 0, sizeof(oled_display::framebuffer));
  memset(oled_display::confirmed, 0, sizeof(oled_display::confirmed));
  oled_display::valid_pages = oled_display::dirty_pages = 0;
  oled_display::page = oled_display::first_column = oled_display::column_count = 0;
  oled_display::next_tick_us = 0;
  oled_display::clock_started = oled_display::gate_open = false;
  oled_display::seconds = oled_display::clock_minute = oled_display::second_in_minute = 0;
  memset(oled_display::diff_first, 0, sizeof(oled_display::diff_first));
  memset(oled_display::diff_last, 0, sizeof(oled_display::diff_last));
  oled_display::uptime_minute = UINT64_MAX;
  oled_display::ip_valid = oled_display::heartbeat = oled_display::web_dot = false;
  browser_connected = false;
  browser_reads = 0;
  oled_display::seen_address_generation = 0;
  oled_display::last_published_status = UINT32_MAX;
  address_generation = 0;
  oled_display::published_status = 0;
  oled_display::address_error = false;
  address_reads = 0;
  snprintf(fake_ip, sizeof(fake_ip), "192.168.1.123");
}

static void service_once(bool allowed = true) {
  uint64_t before = now;
  uint operations = machine.fifo_operations;
  z80_oled_service(allowed);
  assert(now - before <= 8);
  assert(machine.fifo_operations == operations);  // DMA feeds the PIO, not the CPU.
}

// Services and completes DMA until the display is idle and ready.
static void settle() {
  for (unsigned i = 0; i < 3000; ++i) {
    service_once();
    if (channels[0].busy && channels[1].busy)
      run_dma();
    else if (!display_i2c::active && oled_display::state == oled_display::State::ready)
      return;
  }
  assert(false && "Display did not settle");
}

static void run_until_failed() {
  for (unsigned i = 0; i < 100 && oled_display::state != oled_display::State::failed; ++i) {
    service_once();
    if (channels[0].busy && channels[1].busy) run_dma();
  }
  assert(oled_display::state == oled_display::State::failed);
}

static void assert_failed(const char *reason, bool bus_released) {
  assert(oled_display::state == oled_display::State::failed);
  auto before = machine.transactions.size();
  service_once();
  assert(machine.transactions.size() == before);
  diagnostic_output.clear();
  z80_oled_print_status();
  assert(diagnostic_output.find("state=failed") != std::string::npos);
  assert(diagnostic_output.find(reason) != std::string::npos);
  if (bus_released) {
    assert(!machine.enabled && machine.scl && machine.sda);
    assert(!channels[0].claimed && !channels[1].claimed);
  }
}

static bool page_has_pixels(unsigned page) {
  for (unsigned x = 0; x < 128; ++x)
    if (oled_display::confirmed[page * 128 + x]) return true;
  return false;
}

static void test_startup() {
  for (uint8_t oled_address : {0x3c, 0x3d}) {
    reset();
    machine.device_address = oled_address;
    service_once(false);
    assert(!machine.enabled && machine.transactions.empty());
    settle();
    assert(oled_display::enabled && oled_display::address == oled_address);
    assert(oled_display::valid_pages == 0x0f);
    assert(memcmp(oled_display::framebuffer, oled_display::confirmed,
                  sizeof(oled_display::confirmed)) == 0);
    for (unsigned page = 0; page < 4; ++page)
      assert(page_has_pixels(page));  // IP in pages 0-1, uptime in pages 2-3.
    unsigned pages = 0;
    bool panel_on = false, configured = false;
    for (const auto &transaction : machine.transactions) {
      if (transaction[0] != oled_address * 2) continue;
      if (transaction.size() > 2 && transaction[1] == 0 && transaction[2] == 0xae) {
        // 32-row multiplex, sequential COM pins, charge pump on, reduced contrast.
        auto has = [&](uint8_t command, uint8_t value) {
          for (size_t i = 2; i + 1 < transaction.size(); ++i)
            if (transaction[i] == command && transaction[i + 1] == value) return true;
          return false;
        };
        assert(has(0xa8, 0x1f) && has(0xda, 0x02) && has(0x8d, 0x14) && has(0x81, 0x40));
        assert(pages == 0);
        configured = true;
      }
      if (transaction.size() == 136) {
        // One transaction: Co=1 page/column 0 commands, then 128 pixel bytes.
        assert((std::vector<uint8_t>(transaction.begin() + 1, transaction.begin() + 8) ==
                std::vector<uint8_t>{0x80, static_cast<uint8_t>(0xb0 | pages),
                                     0x80, 0x00, 0x80, 0x10, 0x40}));
        assert(memcmp(transaction.data() + 8,
                      oled_display::confirmed + pages * 128, 128) == 0);
        ++pages;
      }
      if (transaction == std::vector<uint8_t>{static_cast<uint8_t>(oled_address * 2), 0, 0xaf}) {
        assert(pages == 4);
        panel_on = true;
      }
    }
    assert(configured && pages == 4 && panel_on);
    assert(machine.minimum_bit_period == 12 && machine.maximum_bit_period == 12);
    assert(120'000'000 / (12 * machine.config.divider) == 400'000);
    assert(machine.minimum_high * machine.config.divider / 120 >= 0.6);
    assert(machine.minimum_low * machine.config.divider / 120 >= 1.3);
    assert(machine.minimum_bus_free * machine.config.divider / 120 >= 1.3);
    auto before = machine.transactions.size();
    service_once(false);
    now = 5'000'000;
    service_once(false);
    assert(machine.transactions.size() == before);  // Paused: nothing new starts.
  }
}

static void test_minute_uptime_and_cached_ip() {
  reset(); settle();
  auto cached_uptime = std::vector<uint8_t>(oled_display::confirmed + 2 * 128,
                                            oled_display::confirmed + 4 * 128);
  for (unsigned second = 1; second < 60; ++second) {
    unsigned reads = address_reads;
    auto writes = machine.transactions.size();
    now = second * 1'000'000;
    settle();
    assert(address_reads == reads);  // Unchanged counter: no address lock or copy...
    // ...and only the heartbeat's two pixel bytes are sent.
    uint8_t dot = second & 1 ? 0xc0 : 0;
    assert(machine.transactions.size() == writes + 1);
    assert((machine.transactions.back() == std::vector<uint8_t>{
        0x78, 0x80, 0xb3, 0x80, 0x0e, 0x80, 0x17, 0x40, dot, dot}));
    assert(oled_display::uptime_minute == 0);
  }
  auto writes = machine.transactions.size();
  now = 60'000'000; settle();
  assert(oled_display::uptime_minute == 1);
  assert(memcmp(cached_uptime.data(), oled_display::confirmed + 2 * 128, 256) != 0);
  // Only uptime pages 2-3 are sent, and only their changed column span.
  assert(machine.transactions.size() > writes && machine.transactions.size() <= writes + 2);
  for (size_t i = writes; i < machine.transactions.size(); ++i) {
    const auto &transaction = machine.transactions[i];
    assert(transaction[2] == 0xb2 || transaction[2] == 0xb3);
    assert(transaction.size() < 136);
  }

  // A marker in the cached IP bitmap survives ticks; an address change redraws it.
  oled_display::framebuffer[128] ^= 0x01;  // Row 8, inside the IP field.
  uint8_t marker = oled_display::framebuffer[128];
  now = 61'000'000; settle();
  assert(oled_display::framebuffer[128] == marker);
  writes = machine.transactions.size();
  set_ip("10.0.0.2");
  now = 62'000'000; settle();
  assert(strcmp(oled_display::cached_ip, fake_ip) == 0);
  assert(oled_display::framebuffer[128] != marker);
  for (size_t i = writes; i < machine.transactions.size(); ++i)  // IP pages, plus heartbeat.
    assert(machine.transactions[i][2] == 0xb0 || machine.transactions[i][2] == 0xb1 ||
           (machine.transactions[i][2] == 0xb3 && machine.transactions[i].size() == 10));
  set_ip("");
  now = 63'000'000; settle();
  assert(strcmp(oled_display::cached_ip, "OFFLINE") == 0);

  // The longest IPv4 address and a multi-day uptime fit the 128-pixel width.
  set_ip("255.255.255.255");
  // Core 0 passes never skip more than the 32-bit timer window while updates
  // run; a pause closes the gate, as this multi-hour jump does.
  service_once(false);
  now = 100ull * 3600 * 1'000'000; settle();
  assert(strcmp(oled_display::cached_ip, "255.255.255.255") == 0);
  // The longest line ends exactly at the right edge: ink in column 127, not beyond.
  bool right_edge = false;
  for (unsigned page = 0; page < 2; ++page)
    right_edge |= oled_display::confirmed[page * 128 + 127] != 0;
  assert(right_edge);
  // Both lines start with their labels at column 0.
  assert(oled_display::confirmed[0] != 0 || oled_display::confirmed[128] != 0);
  assert(oled_display::confirmed[2 * 128] != 0 || oled_display::confirmed[3 * 128] != 0);
}

static void test_idle_fast_path() {
  reset(); settle();
  // Unchanged status is not re-published on idle passes.
  oled_display::published_status = 0xdeadbeef;
  for (unsigned i = 0; i < 100; ++i) service_once();
  assert(oled_display::published_status == 0xdeadbeef);
  // Each idle pass costs one timer read and no transport or address access.
  uint64_t before = now;
  unsigned reads = address_reads;
  auto writes = machine.transactions.size();
  service_once();
  assert(now - before == 1 && address_reads == reads);
  assert(machine.transactions.size() == writes && !display_i2c::active);
  // A changed address published between ticks is picked up on the next tick.
  set_ip("10.1.2.3");
  now = 1'000'000; settle();
  assert(address_reads == reads + 1 && strcmp(oled_display::cached_ip, "10.1.2.3") == 0);
  // Re-publishing the same text bumps the counter but sends no IP pixels.
  writes = machine.transactions.size();
  set_ip("10.1.2.3");
  now = 2'000'000; settle();
  assert(address_reads == reads + 2);
  for (size_t i = writes; i < machine.transactions.size(); ++i)
    assert(machine.transactions[i][2] == 0xb3);  // Heartbeat page only.
}

static void test_heartbeat_bits() {
  // The heartbeat changes only its own two bits and survives uptime redraws.
  reset(); settle();
  assert(!oled_display::heartbeat && oled_display::confirmed[3 * 128 + 126] == 0);
  oled_display::framebuffer[3 * 128 + 126] |= 0x01;
  oled_display::confirmed[3 * 128 + 126] |= 0x01;
  for (uint64_t second = 1; second <= 2; ++second) {
    now = second * 1'000'000; settle();
    assert((oled_display::confirmed[3 * 128 + 126] & 0x01) != 0);
    assert((oled_display::confirmed[3 * 128 + 126] & 0xc0) == (second & 1 ? 0xc0 : 0));
  }
  now = 61'000'000; settle();  // Odd second and a new uptime minute together.
  assert(oled_display::uptime_minute == 1);
  assert((oled_display::confirmed[3 * 128 + 126] & 0xc0) == 0xc0);
  assert((oled_display::confirmed[3 * 128 + 127] & 0xc0) == 0xc0);
  // Paused: the dot freezes, no transfer starts.
  auto writes = machine.transactions.size();
  now = 62'000'000; service_once(false); service_once(false);
  assert(machine.transactions.size() == writes);
}

// Compare the entire row so stale text outside the uptime is also detected.
static void assert_uptime_line(const char *expected) {
  std::vector<uint8_t> drawn(oled_display::framebuffer + 2 * 128,
                             oled_display::framebuffer + 4 * 128);
  oled_display::clear_field(oled_display::UPTIME_Y);
  oled_display::text(oled_display::UPTIME_Y, expected);
  assert(memcmp(drawn.data(), oled_display::framebuffer + 2 * 128, drawn.size()) == 0);
}

// Independent per-pixel reference for the pre-scaled glyph table.
static void reference_text(uint8_t *pixels, unsigned x, unsigned y, const char *value) {
  for (; *value; ++value) {
    uint16_t bits = oled_display::character(*value);
    bool narrow = *value == '.' || *value == ':';
    unsigned columns = narrow ? 1 : *value == ' ' ? 0 : 3;
    for (unsigned row = 0; row < 5; ++row)
      for (unsigned col = 0; col < columns; ++col) {
        unsigned source = narrow ? 1 : col;
        if (!(bits & (1u << (14 - row * 3 - source))))
          continue;
        for (unsigned dy = 0; dy < 2; ++dy)
          for (unsigned dx = 0; dx < 2; ++dx) {
            unsigned px = x + col * 2 + dx, py = y + row * 2 + dy;
            pixels[(py / 8) * 128 + px] |= static_cast<uint8_t>(1u << (py % 8));
          }
      }
    x += (columns + 1) * 2;
  }
}

static void test_text_matches_reference() {
  const char *samples[] = {"0123456789", "EFILNOPRUd", "U: 12:34", "IP: 255.255.255.255",
                           "OFFLINE", "ERROR", "99999999d 23:59"};
  for (unsigned y : {0u, 2u, 3u, 6u, 8u, 20u})
    for (unsigned x : {0u, 1u, 5u})
      for (const char *sample : samples) {
        if (x + oled_display::text_width(sample) > 128) continue;
        uint8_t fast[4 * 128] = {}, expected[4 * 128] = {};
        oled_display::draw_text(fast, 128, 32, x, y, sample);
        reference_text(expected, x, y, sample);
        assert(memcmp(fast, expected, sizeof(fast)) == 0);
      }
  // clear_field clears exactly its ten rows, whatever the page alignment.
  memset(oled_display::framebuffer, 0xff, sizeof(oled_display::framebuffer));
  oled_display::clear_field(oled_display::UPTIME_Y);
  for (unsigned y = 0; y < 32; ++y) {
    bool field = y >= oled_display::UPTIME_Y && y < oled_display::UPTIME_Y + 10;
    for (unsigned x = 0; x < 128; ++x)
      assert(((oled_display::framebuffer[(y / 8) * 128 + x] >> (y % 8)) & 1) == !field);
  }
}

static void test_format_uptime_matches_snprintf() {
  const uint64_t cases[] = {0, 1, 59, 60, 61, 1439, 1440, 1441, 143999, 4294967295ull,
                            4294967296ull, 307445734561ull, UINT64_MAX / 60 / 1'000'000};
  for (uint64_t minutes : cases) {
    char fast[32], expected[32];
    oled_display::format_uptime(fast, minutes);
    unsigned long long hours = minutes / 60, days = hours / 24;
    if (days)
      snprintf(expected, sizeof(expected), "U: %llud %02llu:%02llu", days, hours % 24, minutes % 60);
    else
      snprintf(expected, sizeof(expected), "U: %02llu:%02llu", hours, minutes % 60);
    if (days && oled_display::text_width(expected) > 128)
      memmove(expected, expected + 3, strlen(expected) - 2);
    assert(strcmp(fast, expected) == 0);
  }
}

// Counting ticks must land exactly where dividing the timer would.
static void test_incremental_clock() {
  reset(); settle();
  for (uint64_t second = 1; second <= 3 * 60 + 5; ++second) {
    now = second * 1'000'000; settle();
    assert(oled_display::seconds == second);
    assert(oled_display::clock_minute == second / 60);
    assert(oled_display::second_in_minute == second % 60);
    assert(oled_display::next_tick_us == (second + 1) * 1'000'000);
  }
  // A skipped tick resynchronises instead of drifting.
  now = 500'500'000; settle();
  assert(oled_display::seconds == 500 && oled_display::clock_minute == 8 &&
         oled_display::second_in_minute == 20);
}

static void test_uptime_format() {
  constexpr uint64_t hour_us = 3600ull * 1'000'000;
  constexpr uint64_t minute_us = 60ull * 1'000'000;
  reset(); settle();
  // Jumps beyond the 32-bit timer window follow a pause, which closes the gate.
  auto jump = [](uint64_t time) { service_once(false); now = time; settle(); };
  assert_uptime_line("U: 00:00");
  jump(23 * hour_us + 59 * minute_us);
  assert_uptime_line("U: 23:59");
  jump(24 * hour_us);
  assert_uptime_line("U: 1d 00:00");
  jump((4 * 24 + 5) * hour_us + 23 * minute_us);
  assert_uptime_line("U: 4d 05:23");
  jump((999ull * 24 + 23) * hour_us + 59 * minute_us);
  assert_uptime_line("U: 999d 23:59");
  jump((1000ull * 24 + 1) * hour_us + 2 * minute_us);
  assert_uptime_line("U: 1000d 01:02");
  jump((99'999'999ull * 24 + 23) * hour_us + 59 * minute_us);
  assert_uptime_line("U: 99999999d 23:59");
  jump(100'000'000ull * 24 * hour_us);
  assert_uptime_line("100000000d 00:00");
  jump(123'456'789ull * 24 * hour_us);
  assert_uptime_line("123456789d 00:00");
  jump(UINT64_MAX - 2'000'000);
  assert_uptime_line("213503982d 08:01");
}

static void test_web_terminal_dot() {
  constexpr unsigned web = 3 * 128 + 122, beat = 3 * 128 + 126;
  reset(); settle();
  // No browser: only the heartbeat's two bytes are sent, exactly as before.
  auto writes = machine.transactions.size();
  now = 1'000'000; settle();
  assert(machine.transactions.size() == writes + 1);
  assert((machine.transactions.back() == std::vector<uint8_t>{
      0x78, 0x80, 0xb3, 0x80, 0x0e, 0x80, 0x17, 0x40, 0xc0, 0xc0}));
  assert(oled_display::confirmed[web] == 0 && oled_display::confirmed[web + 1] == 0);

  // Connected: both dots blink together, in one transaction per second.
  oled_display::framebuffer[web] |= 0x01;  // A neighbouring pixel survives.
  oled_display::confirmed[web] |= 0x01;
  browser_connected = true;
  for (uint64_t second = 2; second <= 5; ++second) {
    writes = machine.transactions.size();
    unsigned reads = browser_reads;
    now = second * 1'000'000; settle();
    // Read only on lit seconds: dark seconds clear both dots regardless.
    assert(browser_reads == reads + (second & 1));
    uint8_t dot = second & 1 ? 0xc0 : 0;
    assert(machine.transactions.size() == writes + 1);
    if (second == 2)  // Web dot already dark: only the heartbeat span is sent.
      assert((machine.transactions.back() == std::vector<uint8_t>{
          0x78, 0x80, 0xb3, 0x80, 0x0e, 0x80, 0x17, 0x40, 0, 0}));
    else
      assert((machine.transactions.back() == std::vector<uint8_t>{
          0x78, 0x80, 0xb3, 0x80, 0x0a, 0x80, 0x17, 0x40,
          static_cast<uint8_t>(dot | 0x01), dot, 0, 0, dot, dot}));
    assert((oled_display::confirmed[web] & 0xc0) == dot);
    assert((oled_display::confirmed[web + 1] & 0xc0) == dot);
    assert((oled_display::confirmed[beat] & 0xc0) == dot);
    assert(oled_display::confirmed[web] & 0x01);
  }

  // Idle passes between ticks do not read the browser state.
  unsigned reads = browser_reads;
  for (unsigned i = 0; i < 100; ++i) service_once();
  assert(browser_reads == reads);

  // Disconnect while lit: the next tick clears the web dot and the heartbeat.
  now = 7'000'000; settle();
  assert(oled_display::web_dot);
  browser_connected = false;
  now = 8'000'000; settle();
  assert(!oled_display::web_dot && (oled_display::confirmed[web] & 0xc0) == 0);
  writes = machine.transactions.size();
  now = 9'000'000; settle();
  assert((machine.transactions.back() == std::vector<uint8_t>{
      0x78, 0x80, 0xb3, 0x80, 0x0e, 0x80, 0x17, 0x40, 0xc0, 0xc0}));
  assert(machine.transactions.size() == writes + 1);
  assert((oled_display::confirmed[web] & 0xc0) == 0);

  // Paused: both dots freeze and nothing is sent.
  browser_connected = true;
  writes = machine.transactions.size();
  now = 10'000'000; service_once(false); service_once(false);
  assert(machine.transactions.size() == writes);
}

static void test_chaining_and_pause() {
  // Completed pages chain into the next page in the same service pass.
  reset(); settle();
  set_ip("10.0.0.2");
  now = 1'000'000;
  service_once();  // Renders and starts the first page in one pass.
  assert(oled_display::transfer_active);
  unsigned expected = __builtin_popcount(oled_display::dirty_pages);
  assert(expected == 3);  // Both IP pages plus the heartbeat page.
  unsigned sent = 0;
  while (oled_display::transfer_active) {
    run_dma(); service_once(); ++sent;
  }
  assert(sent == expected && oled_display::state == oled_display::State::ready);

  // A pause drains the in-flight page through STOP but starts no new page.
  reset(); settle();
  set_ip("10.0.0.2");
  now = 1'000'000;
  service_once();
  assert(oled_display::transfer_active);
  auto words = std::vector<uint32_t>(display_i2c::commands,
                                     display_i2c::commands + channels[0].count);
  assert(!display_i2c::begin(0x3c, nullptr, 0));  // In-flight buffers are protected.
  service_once(false);
  assert(std::equal(words.begin(), words.end(), display_i2c::commands));
  run_dma();
  service_once(false);
  assert(!oled_display::transfer_active && !display_i2c::active);
  service_once(false);
  assert(!display_i2c::active);
  service_once();
  assert(oled_display::transfer_active);
}

static void test_device_failures() {
  reset(); machine.device_address = 0;
  run_until_failed();
  assert(oled_display::address == 0x3d);  // Both addresses were scanned.
  assert_failed("no OLED ACK at 0x3c or 0x3d", false);

  // Initialization NACK: no panel-on, display disabled.
  reset();
  for (unsigned i = 0; i < 100 && oled_display::state != oled_display::State::initialize; ++i) {
    service_once();
    if (channels[0].busy && channels[1].busy) run_dma();
  }
  machine.nack_byte = 2;
  run_until_failed();
  assert(!oled_display::enabled);
  assert_failed("SSD1315 write was not acknowledged", false);

  // A NACK on any byte of a maximum-length write is reported, not a bus failure.
  for (unsigned byte = 1; byte <= display_i2c::MAX_DATA_BYTES + 1; ++byte) {
    reset(); machine.nack_byte = byte;
    uint8_t data[display_i2c::MAX_DATA_BYTES] = {0x80};
    assert(display_i2c::begin(0x3c, data, sizeof(data)));
    assert(channels[0].count == display_i2c::MAX_DATA_BYTES + 3 &&
           channels[1].count == display_i2c::MAX_DATA_BYTES + 2);
    run_dma();
    assert(display_i2c::poll() == display_i2c::Result::nack);
    assert(display_i2c::failure() == display_i2c::Failure::none);
    machine.nack_byte = 0;
    assert(display_i2c::begin(0x3c, nullptr, 0));
    run_dma();
    assert(display_i2c::poll() == display_i2c::Result::complete);
  }
}

static void test_transport_failures() {
  const char *timeout = "PIO/DMA transfer timeout or missing STOP/ACK result";

  // TX done but the STOP result never arrives.
  reset(); settle();
  set_ip("10.0.0.2");  // Start an IP page write.
  now = 1'000'000; service_once();
  assert(display_i2c::active);
  run_dma(false);
  now = display_i2c::deadline;
  service_once(false);
  assert(display_i2c::failure() == display_i2c::Failure::timeout);
  assert_failed(timeout, true);

  // DMA idle before writing every result.
  reset(); settle();
  set_ip("10.0.0.2");  // Start an IP page write.
  now = 1'000'000; service_once();
  channels[0].busy = channels[1].busy = false;
  service_once();
  assert_failed(timeout, true);

  // A result slot left at its sentinel value.
  reset();
  assert(display_i2c::begin(0x3c, nullptr, 0));
  run_dma();
  display_i2c::results[0] = UINT32_MAX;
  assert(display_i2c::poll() == display_i2c::Result::failed);
  assert(!machine.enabled && !channels[0].claimed && !channels[1].claimed);

  // A stalled PIO state machine reaches the transfer deadline.
  reset(); service_once(); machine.stalled = true;
  now = display_i2c::deadline;
  service_once();
  assert_failed(timeout, true);

  // Resource allocation failures release any partial claim.
  reset(); channels[0].claimed = true;  // Another user holds one channel.
  service_once(); service_once();  // Failure is observed next pass.
  assert_failed("no DMA channels available", false);
  assert(!machine.enabled && machine.scl && machine.sda);
  assert(channels[0].claimed && !channels[1].claimed);
  reset(); dma_available = false; service_once(); service_once();  // Failure is observed next pass.
  assert_failed("no DMA channels available", true);
  reset(); machine.available = false; service_once(); service_once();  // Failure is observed next pass.
  assert_failed("no PIO instruction memory for I2C program", true);
  reset(); machine.sm_available = false; service_once(); service_once();  // Failure is observed next pass.
  assert_failed("no PIO state machine available", true);
}

static void test_shared_bus_reads() {
  reset();
  machine.device_address = 0x42;
  machine.read_bytes = {0, 0xff, 0x80, 0x01, 0x55, 0xaa};
  machine.stretch_cycles = 20;
  uint8_t data[6] = {};
  assert(display_i2c::begin_read(0x42, data, sizeof(data), display_i2c::Speed::fast));
  assert(display_i2c::busy());
  assert(!display_i2c::begin(0x3c, nullptr, 0));
  assert(machine.instructions.size() + 4 <= 32); // Alongside the Z80 clock.
  assert(machine.config.divider == 25.0f);  // Fast-mode: 400 kHz.
  run_dma();
  assert(display_i2c::poll() == display_i2c::Result::complete);
  assert(std::equal(std::begin(data), std::end(data), machine.read_bytes.begin()));
  assert((machine.read_acks == std::vector<bool>{true, true, true, true, true, false}));
  assert(machine.minimum_high >= 5 && machine.minimum_low >= 7);
  assert(!display_i2c::busy());

  // An absent speaker NACKs without poisoning the shared OLED bus.
  machine.device_address = 0;
  assert(display_i2c::begin_read(0x42, data, sizeof(data), display_i2c::Speed::fast));
  run_dma();
  assert(display_i2c::poll() == display_i2c::Result::nack);
  machine.device_address = 0x3c;
  assert(display_i2c::begin(0x3c, nullptr, 0));
  assert(machine.config.divider == 100.0f);  // The OLED returns to 100 kHz after a fast transfer.
  run_dma();
  assert(display_i2c::poll() == display_i2c::Result::complete);

  // OLED yields to a shared-bus read instead of consuming its DMA result.
  reset(); settle();
  machine.device_address = 0x42;
  machine.read_bytes = {1};
  assert(display_i2c::begin_read(0x42, data, 1));
  now = 1'000'000;
  service_once();
  assert(!oled_display::transfer_active);
  run_dma();
  service_once();
  assert(display_i2c::busy());
  assert(display_i2c::poll() == display_i2c::Result::complete && data[0] == 1);
  machine.device_address = 0x3c;
  settle();
  assert(oled_display::state == oled_display::State::ready);
}

int main() {
  test_startup();
  test_minute_uptime_and_cached_ip();
  test_idle_fast_path();
  test_uptime_format();
  test_text_matches_reference();
  test_format_uptime_matches_snprintf();
  test_incremental_clock();
  test_heartbeat_bits();
  test_web_terminal_dot();
  test_chaining_and_pause();
  test_device_failures();
  test_transport_failures();
  test_shared_bus_reads();
  puts("PASS: SSD1315 128x32 PIO/DMA transport, merged page writes, same-pass chaining, "
       "cached IP, full-width minute-only uptime with days format, "
       "bit-safe heartbeat and web-terminal dot, idle fast path, pause gating and failures");
}
