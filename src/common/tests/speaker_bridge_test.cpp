#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "../../cpm/dcc_io_adapter/host/PortDrivers/speaker_io.h"
}
#include "../../stage10_websocket_terminal/speaker_io.cpp"

static uint64_t now;
static bool bus_busy, bus_read;
static uint8_t *bus_destination;
static size_t bus_length;
static std::vector<uint8_t> bus_packet;
static display_i2c::Result bus_result;
static unsigned reads, writes, pending_reads;
static bool wrong_version, never_execute;

uint64_t time_us_64() { return now; }

namespace display_i2c {
bool busy() { return bus_busy; }
bool begin(uint8_t address, const uint8_t *data, size_t length, uint32_t baud) {
  assert(address == ATTINY85_I2C_ADDRESS && baud == 100000 && !bus_busy);
  bus_packet.assign(data, data + length);
  bus_busy = true;
  bus_read = false;
  ++writes;
  return true;
}
bool begin_read(uint8_t address, uint8_t *data, size_t length, uint32_t baud) {
  assert(address == ATTINY85_I2C_ADDRESS && baud == 100000 && !bus_busy);
  assert(length == STATUS_BYTES);
  bus_destination = data;
  bus_length = length;
  bus_read = bus_busy = true;
  ++reads;
  return true;
}
Result poll() {
  assert(bus_busy);
  if (bus_result == Result::pending)
    return Result::pending;
  bus_busy = false;
  if (bus_result != Result::complete)
    return bus_result;
  if (!bus_read) {
    speaker_io_input();
    for (uint8_t byte : bus_packet)
      speaker_io_output(byte);
  } else {
    speaker_io_input();
    speaker_io_output(0);
    for (size_t i = 0; i < bus_length; ++i)
      bus_destination[i] = speaker_io_input();
    if (pending_reads || never_execute) {
      bus_destination[S_FLAGS] |= STATUS_COMMAND_PENDING;
      if (pending_reads) --pending_reads;
    }
    if (wrong_version) bus_destination[S_VERSION] = 0xff;
  }
  return Result::complete;
}
}

static void reset() {
  speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
  produced = consumed = fault = transfers = 0;
  position = expected = 0;
  cursor = STATUS_BYTES;
  discarding = latch = disabled = false;
  state = State::idle;
  memset(status, 0, sizeof(status));
  memset(transmit, 0, sizeof(transmit));
  now = deadline = retry_at = 0;
  bus_busy = bus_read = false;
  bus_result = display_i2c::Result::complete;
  pending_reads = reads = writes = 0;
  wrong_version = never_execute = false;
}

static void settle() {
  for (unsigned i = 0; i < 1000 && !z80_speaker_quiescent(); ++i) {
    z80_speaker_core1_service();
    now += 100;
  }
  assert(z80_speaker_quiescent());
}

static void packet_out(std::initializer_list<uint8_t> bytes) {
  assert(z80_speaker_read_ready(Z80_SPEAKER_PORT));
  z80_speaker_in();
  for (uint8_t byte : bytes)
    z80_speaker_out(byte);
}

static void read_status(uint8_t *data) {
  settle(); // The first IN is a barrier for the previous command.
  z80_speaker_in();
  z80_speaker_out(0);
  assert(!z80_speaker_read_ready(Z80_SPEAKER_PORT) || disabled);
  settle();
  for (unsigned i = 0; i < STATUS_BYTES; ++i)
    data[i] = z80_speaker_in();
  assert(z80_speaker_in() == 0xff);
}

static void tick(unsigned milliseconds) {
  float samples[48];
  for (unsigned i = 0; i < milliseconds; ++i)
    speaker_io_render(samples, 48);
}

static void test_packets_and_snapshots() {
  reset();
  uint8_t data[STATUS_BYTES];
  read_status(data);
  assert(data[S_VERSION] == PROTOCOL_VERSION && !data[S_ACTIVE_ID]);
  // Neither core-0 packet collection nor its readiness gate performs I2C.
  packet_out({COMMAND_PLAY, 1, 2, 0, 100, 100, FX_PRESET, PRESET_COIN});
  assert(writes == 0 && !z80_speaker_read_ready(Z80_SPEAKER_PORT));
  assert(z80_speaker_read_ready(0x10));
  pending_reads = 3;
  read_status(data);
  assert(writes == 1 && reads == 6);
  assert(data[S_REQUEST_ID] == 1 && data[S_RESULT] == RESULT_ACCEPTED);
  assert(data[S_ACTIVE_ID] == 1 && data[S_PRESET] == PRESET_COIN);
  tick(500);
  read_status(data);
  assert(!data[S_ACTIVE_ID] && read_be16(data + S_COMPLETED_HI) == 1);
  // A latched snapshot cannot change part-way through its byte stream.
  z80_speaker_out(0); settle();
  assert(z80_speaker_in() == PROTOCOL_VERSION);
  tick(1000);
  for (unsigned i = 1; i < STATUS_BYTES; ++i)
    assert(z80_speaker_in() == data[i]);
  packet_out({COMMAND_ACK_EVENT, data[S_EVENT_SEQ_HI], data[S_EVENT_SEQ_LO]});
  read_status(data);
  assert(data[S_EVENT_TYPE] == EVENT_COMPLETED);
}

static void test_presets_and_controls() {
  reset();
  uint8_t data[STATUS_BYTES];
  for (uint8_t preset = 1; preset < PRESET_COUNT; ++preset) {
    packet_out({COMMAND_PLAY, preset, 1, 0, 100, 100, FX_PRESET, preset});
    read_status(data);
    assert(data[S_REQUEST_ID] == preset && data[S_PRESET] == preset);
    assert(data[S_RESULT] == RESULT_ACCEPTED);
  }
  packet_out({COMMAND_PLAY, 64, 1, 0, 100, 100, FX_PRESET, PRESET_ENGINE});
  settle();
  packet_out({COMMAND_UPDATE, 65, 64, UPDATE_PITCH, 150, 100, 0, 0, 0, 0});
  read_status(data);
  assert(data[S_RESULT] == RESULT_UPDATED && data[S_ACTIVE_ID] == 64);
  packet_out({COMMAND_PLAY, 66, 1, PLAY_ENQUEUE, 100, 100, FX_PRESET, PRESET_COIN});
  read_status(data);
  assert(data[S_RESULT] == RESULT_QUEUED && data[S_QUEUED] == 1);
  packet_out({COMMAND_CLEAR_QUEUE, 67});
  read_status(data);
  assert(data[S_RESULT] == RESULT_CLEARED && data[S_QUEUED] == 0);
  packet_out({COMMAND_STOP, 68});
  read_status(data);
  assert(data[S_RESULT] == RESULT_STOPPED && data[S_ACTIVE_ID] == 0);
}

static void test_manual_effects() {
  reset();
  uint8_t data[STATUS_BYTES];
  const std::vector<std::vector<uint8_t>> effects = {
      {FX_TONE, 3, 0xe8, 0, 100},
      {FX_SWEEP, 3, 0xe8, 7, 0xd0, 0, 100},
      {FX_SIREN, 3, 0xe8, 7, 0xd0, 0, 100, 0, 100, 1, 0},
      {FX_SEQUENCE, 2, 0, 1, 3, 0xe8, 0, 100, 0, 0, 0, 100},
      {FX_SEGMENTS, 1, 0, 1, 3, 0xe8, 7, 0xd0, 0, 100},
      {FX_PULSES, 3, 3, 0xe8, 0, 20, 0, 40, 0, 10},
      {FX_VIBRATO, 3, 0xe8, 7, 0xd0, 1, 0, 0, 80, 0, 100},
      {FX_ARPEGGIO, 3, 0xe8, 0, 50, 1, 0, 1},
      {FX_GATE, 3, 0xe8, 0, 20, 0, 40, 1, 0},
      {FX_PRESET, PRESET_COIN},
  };
  for (const auto &effect : effects) {
    uint8_t id = effect[0];
    z80_speaker_in();
    for (uint8_t b : {COMMAND_PLAY, (unsigned)id, 1u, 0u, 100u, 100u})
      z80_speaker_out(b);
    for (uint8_t b : effect) z80_speaker_out(b);
    read_status(data);
    assert(data[S_REQUEST_ID] == id && data[S_RESULT] == RESULT_ACCEPTED);
    assert(data[S_ACTIVE_ID] == id);
    assert(bus_packet.size() == effect.size() + 6);
  }
  // A command following OUT 0 without an IN must not mutate that snapshot.
  z80_speaker_in();
  z80_speaker_out(0);
  settle();
  const uint8_t latched_id = status[S_ACTIVE_ID];
  z80_speaker_out(COMMAND_STOP);
  z80_speaker_out(100);
  settle();
  for (unsigned i = 0; i < STATUS_BYTES; ++i) data[i] = z80_speaker_in();
  assert(data[S_ACTIVE_ID] == latched_id);
}

static void test_framing_and_bounds() {
  reset();
  uint8_t data[STATUS_BYTES];
  // Bad framing discards OUT 0 as well; only IN recovers it.
  z80_speaker_out(0xff);
  z80_speaker_out(0);
  assert(produced == 0);
  z80_speaker_in();
  packet_out({COMMAND_PLAY, 1, 1, 0, 100, 100, FX_SEQUENCE, 9});
  assert(produced == 0 && discarding);
  z80_speaker_in();
  // Invalid complete packets preserve the packet boundary.
  packet_out({COMMAND_PLAY, 1, 1, 0, 100, 100, FX_PRESET, 0});
  assert(produced == 0 && !discarding);
  // Maximum-length sequence and segment packets fit the fixed buffer.
  for (uint8_t effect : {FX_SEQUENCE, FX_SEGMENTS}) {
    unsigned count = effect == FX_SEQUENCE ? 8 : 7;
    z80_speaker_in();
    for (uint8_t b : {COMMAND_PLAY, 2u, 1u, 0u, 100u, 100u, (unsigned)effect,
                      count, 0u, 1u})
      z80_speaker_out(b);
    if (effect == FX_SEGMENTS) { z80_speaker_out(3); z80_speaker_out(0xe8); }
    for (unsigned i = 0; i < count; ++i)
      for (uint8_t b : {3, 0xe8, 0, 10}) z80_speaker_out(b);
    assert(!position && !discarding);
    settle();
  }
  // Raw bursts are bounded; overflow is explicit and releases deferred reads.
  reset();
  for (unsigned i = 0; i < QUEUE_SIZE + 1; ++i)
    z80_speaker_out(0);
  assert(fault == QUEUE_OVERFLOW && !z80_speaker_read_ready(Z80_SPEAKER_PORT));
  settle();
  assert(disabled && status[S_VERSION] == 0 && status[S_RESULT] == RESULT_INVALID);
  assert(z80_speaker_read_ready(Z80_SPEAKER_PORT));
  reset();
  for (uint8_t id = 1; id <= QUEUE_SIZE; ++id) {
    z80_speaker_out(COMMAND_STOP);
    z80_speaker_out(id);
  }
  assert(produced == QUEUE_SIZE && !consumed);
  settle();
  read_status(data);
  assert(writes == QUEUE_SIZE && !fault && data[S_REQUEST_ID] == QUEUE_SIZE);
  // SPSC counters remain valid across wraparound.
  reset(); produced = consumed = UINT32_MAX;
  z80_speaker_out(0); settle();
  assert(produced == 0 && consumed == 0);
}

static void test_failures_and_bus_sharing() {
  uint8_t data[STATUS_BYTES];
  for (auto result : {display_i2c::Result::nack, display_i2c::Result::failed}) {
    reset();
    bus_result = result;
    z80_speaker_out(0); settle();
    assert(disabled && fault == (result == display_i2c::Result::nack
                                 ? DEVICE_NACK : BUS_FAILURE));
    read_status(data);
    assert(data[S_VERSION] == 0 && !data[S_ACTIVE_ID]);
  }
  reset(); wrong_version = true;
  z80_speaker_out(0); settle();
  assert(disabled && fault == PROTOCOL_MISMATCH);
  reset(); never_execute = true;
  packet_out({COMMAND_STOP, 1}); settle();
  assert(disabled && fault == EXECUTION_TIMEOUT);
  reset();
  bus_busy = true; // OLED owns the bus; speaker must yield without polling it.
  z80_speaker_out(0);
  for (unsigned i = 0; i < 10; ++i) z80_speaker_core1_service();
  assert(!reads && state == State::idle);
  bus_busy = false;
  settle();
  assert(reads == 1 && !fault);
}

int main() {
  test_packets_and_snapshots();
  test_presets_and_controls();
  test_manual_effects();
  test_framing_and_bounds();
  test_failures_and_bus_sharing();
  speaker_io_close();
  puts("PASS: Stage 10 speaker bridge, real ATtiny85 engine, all presets, controls, "
       "snapshot barriers, framing, fixed bounds, wrap, bus sharing and failures");
}
