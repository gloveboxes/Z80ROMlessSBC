#include "speaker_io.h"
#include "display_i2c.h"
#include "../stage11_extension_board/flash_ATTiny85/target/protocol.h"
#include "z80sbc/ram_func.h"
#include "z80sbc/ram_timer.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr unsigned QUEUE_SIZE = 4;
struct Request {
  uint8_t size;
  uint8_t bytes[COMMAND_MAX_BYTES];
};
Request requests[QUEUE_SIZE];
uint32_t produced, consumed;
uint32_t fault;
enum : uint32_t {
  NO_FAULT, QUEUE_OVERFLOW, DEVICE_NACK, BUS_FAILURE, PROTOCOL_MISMATCH,
  EXECUTION_TIMEOUT,
};
// Published only before consumed advances; core 0 reads after an acquire.
uint8_t status[STATUS_BYTES];
uint8_t fetched[STATUS_BYTES];
uint8_t transmit[STATUS_BYTES];
uint8_t packet[COMMAND_MAX_BYTES];
uint8_t position, expected, cursor = STATUS_BYTES;
bool discarding, latch;
enum class State : uint8_t { idle, write, read, execute };
State state;
bool disabled;
uint64_t deadline, retry_at;
uint32_t transfers;

void __not_in_flash_func(enqueue)(uint8_t size) {
  if (__atomic_load_n(&fault, __ATOMIC_ACQUIRE))
    return;
  uint32_t head = produced;
  if (head - __atomic_load_n(&consumed, __ATOMIC_ACQUIRE) == QUEUE_SIZE) {
    __atomic_store_n(&fault, QUEUE_OVERFLOW, __ATOMIC_RELEASE);
    return;
  }
  Request &request = requests[head & (QUEUE_SIZE - 1)];
  request.size = size;
  for (unsigned i = 0; i < size; ++i)
    request.bytes[i] = packet[i];
  __atomic_store_n(&produced, head + 1, __ATOMIC_RELEASE);
}

void fail(uint32_t reason) {
  __atomic_store_n(&fault, reason, __ATOMIC_RELEASE);
  disabled = true;
  state = State::idle;
  memset(status, 0, sizeof(status));
  status[S_RESULT] = RESULT_INVALID;
  __atomic_store_n(&consumed, __atomic_load_n(&produced, __ATOMIC_ACQUIRE),
                   __ATOMIC_RELEASE);
  constexpr const char *reasons[] = {
      "none", "command queue overflow", "ATtiny85 did not acknowledge",
      "I2C bus failure", "ATtiny85 protocol version mismatch",
      "ATtiny85 command execution timeout",
  };
  printf("[speaker] FAIL: %s; sound disabled until reboot.\n", reasons[reason]);
}

bool start_read() {
  if (display_i2c::begin_read(ATTINY85_I2C_ADDRESS, fetched, sizeof(fetched)))
    return true;
  fail(BUS_FAILURE);
  return false;
}
}

extern "C" void __not_in_flash_func(z80_speaker_out)(uint8_t value) {
  if (discarding)
    return;
  if (!position && !value) {
    enqueue(0);
    latch = true;
    return;
  }
  packet[position++] = value;
  if (position == 1) {
    switch (value) {
    case COMMAND_PLAY: expected = 8; break;
    case COMMAND_STOP:
    case COMMAND_CLEAR_QUEUE: expected = 2; break;
    case COMMAND_UPDATE: expected = 10; break;
    case COMMAND_ACK_EVENT: expected = 3; break;
    default: discarding = true; break;
    }
  }
  if (packet[0] == COMMAND_PLAY && position == 8) {
    if ((packet[6] == FX_SEQUENCE && (!value || value > SEQUENCE_MAX_STEPS)) ||
        (packet[6] == FX_SEGMENTS && (!value || value > SEGMENT_MAX_STEPS))) {
      discarding = true;
    } else {
      expected = packet_length(packet);
      if (!expected || expected > COMMAND_MAX_BYTES)
        discarding = true;
    }
  }
  if (discarding) {
    position = 0;
  } else if (position == expected) {
    if (command_valid(packet))
      enqueue(position);
    position = 0;
  }
}

extern "C" bool __not_in_flash_func(z80_speaker_read_ready)(uint8_t port) {
  return port != Z80_SPEAKER_PORT ||
         __atomic_load_n(&consumed, __ATOMIC_ACQUIRE) == produced;
}

extern "C" uint8_t __not_in_flash_func(z80_speaker_in)(void) {
  position = expected = 0;
  discarding = false;
  if (latch) {
    for (unsigned i = 0; i < STATUS_BYTES; ++i)
      transmit[i] = status[i];
    cursor = 0;
    latch = false;
  }
  return cursor < STATUS_BYTES ? transmit[cursor++] : 0xff;
}

extern "C" void z80_speaker_core1_service(void) {
  if (disabled)
    return;
  uint32_t error = __atomic_load_n(&fault, __ATOMIC_ACQUIRE);
  if (error && state == State::idle) {
    fail(error);
    return;
  }
  uint32_t tail = consumed;
  const Request &request = requests[tail & (QUEUE_SIZE - 1)];
  if (state == State::idle) {
    if (tail == __atomic_load_n(&produced, __ATOMIC_ACQUIRE) ||
        display_i2c::busy())
      return;
    deadline = z80_time_us_64() + 50000;
    if (request.size) {
      if (!display_i2c::begin(ATTINY85_I2C_ADDRESS, request.bytes, request.size, 100000)) {
        fail(BUS_FAILURE);
        return;
      }
      state = State::write;
    } else if (start_read()) {
      state = State::read;
    }
    return;
  }
  if (state == State::execute) {
    if (z80_time_us_64() < retry_at || display_i2c::busy())
      return;
    if (start_read())
      state = State::read;
    return;
  }
  auto result = display_i2c::poll();
  if (result == display_i2c::Result::pending)
    return;
  if (result != display_i2c::Result::complete) {
    fail(result == display_i2c::Result::nack ? DEVICE_NACK : BUS_FAILURE);
    return;
  }
  __atomic_fetch_add(&transfers, 1u, __ATOMIC_RELAXED);
  if (state == State::write) {
    state = State::execute;
    retry_at = z80_time_us_64();
    return;
  }
  if (fetched[S_VERSION] != PROTOCOL_VERSION) {
    fail(PROTOCOL_MISMATCH);
    return;
  }
  bool pending = fetched[S_FLAGS] & STATUS_COMMAND_PENDING;
  if (request.size) {
    if (request.bytes[0] == COMMAND_ACK_EVENT)
      pending |= fetched[S_EVENT_TYPE] &&
                 read_be16(fetched + S_EVENT_SEQ_HI) == read_be16(request.bytes + 1);
    else
      pending |= fetched[S_REQUEST_ID] != request.bytes[1];
  }
  if (pending) {
    if (z80_time_us_64() >= deadline) {
      fail(EXECUTION_TIMEOUT);
      return;
    }
    retry_at = z80_time_us_64() + 200;
    state = State::execute;
    return;
  }
  if (!request.size)
    memcpy(status, fetched, sizeof(status));
  state = State::idle;
  __atomic_store_n(&consumed, tail + 1, __ATOMIC_RELEASE);
}

extern "C" bool z80_speaker_quiescent(void) {
  return state == State::idle &&
         consumed == __atomic_load_n(&produced, __ATOMIC_ACQUIRE);
}

extern "C" void z80_speaker_print_status(void) {
  printf("[speaker] port=15h address=42h SDA=GP22 SCL=GP26 baud=100000 "
         "fault=%lu transfers=%lu\n",
         (unsigned long)__atomic_load_n(&fault, __ATOMIC_ACQUIRE),
         (unsigned long)__atomic_load_n(&transfers, __ATOMIC_RELAXED));
}
