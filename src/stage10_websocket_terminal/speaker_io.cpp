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
  NO_FAULT, BUS_FAILURE, PROTOCOL_MISMATCH, SERVICE_STALL,
};
// Published only before consumed advances; the IRQ reads after an acquire.
uint8_t status[STATUS_BYTES];
uint8_t fetched[STATUS_BYTES];
uint8_t transmit[STATUS_BYTES];
uint8_t packet[COMMAND_MAX_BYTES];
uint8_t position, expected, cursor = STATUS_BYTES;
bool discarding, latch;
enum class State : uint8_t { idle, write, read, execute };
State state;
// Written last by fail() so the IRQ sees the released queue and cleared status.
bool disabled, reported, deferring;
uint64_t deadline, retry_at, next_at, deferred_since;
uint32_t stall_state, stall_backlog;
uint64_t last_service;
uint32_t transfers, dropped;
// Commands are fire and forget. The ATtiny85 has one command slot and NACKs a
// byte while it is full, so writes are paced; a command it still refuses, or
// that arrives with the queue full, is dropped and counted.
constexpr uint64_t WRITE_GAP_US = 500;
constexpr unsigned STATUS_POLL_US = 200;
// A deferred IN holds the Z80 in WAIT, and the I/O watchdog resets the Pico
// at 500 ms. A bridge that has not answered by then is disabled instead.
constexpr uint64_t DEFERRAL_LIMIT_US = 100000;
constexpr uint64_t MAX_SERVICE_GAP_US = 2000;

void __not_in_flash_func(enqueue)(uint8_t size) {
  if (__atomic_load_n(&fault, __ATOMIC_ACQUIRE))
    return;
  uint32_t head = produced;
  if (head - __atomic_load_n(&consumed, __ATOMIC_ACQUIRE) == QUEUE_SIZE) {
    __atomic_fetch_add(&dropped, 1u, __ATOMIC_RELAXED);
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
  state = State::idle;
  memset(status, 0, sizeof(status));
  status[S_RESULT] = RESULT_INVALID;
  __atomic_store_n(&consumed, __atomic_load_n(&produced, __ATOMIC_ACQUIRE),
                   __ATOMIC_RELEASE);
  // Core 0 may still publish one request after the consumed store above.
  // Every IN is released once the bridge is disabled, so it cannot be stranded.
  __atomic_store_n(&disabled, true, __ATOMIC_RELEASE);
  constexpr const char *reasons[] = {
      "none", "I2C bus failure", "ATtiny85 protocol version mismatch",
      "bridge service stalled",
  };
  reported = true;
  printf("[speaker] FAIL: %s; sound disabled until reboot.\n", reasons[reason]);
}

bool __not_in_flash_func(start_read)() {
  if (display_i2c::begin_read(ATTINY85_I2C_ADDRESS, fetched, sizeof(fetched)))
    return true;
  fail(BUS_FAILURE);
  return false;
}

bool __not_in_flash_func(start_write)(const Request &request) {
  if (display_i2c::begin(ATTINY85_I2C_ADDRESS, request.bytes, request.size))
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

extern "C" bool __not_in_flash_func(z80_speaker_read_ready)(void) {
  if (__atomic_load_n(&disabled, __ATOMIC_ACQUIRE))
    return true;
  uint32_t backlog = produced - __atomic_load_n(&consumed, __ATOMIC_ACQUIRE);
  if (!backlog) {
    deferring = false;
    return true;
  }
  uint64_t now = z80_time_us_64();
  if (!deferring) {
    deferring = true;
    deferred_since = now;
  }
  if (now - deferred_since < DEFERRAL_LIMIT_US)
    return false;
  // The request did not complete in time: release the Z80 with an invalid
  // status. The service reports the fault and finishes any transfer.
  deferring = false;
  stall_state = static_cast<uint32_t>(state);
  stall_backlog = backlog;
  memset(status, 0, sizeof(status));
  status[S_RESULT] = RESULT_INVALID;
  __atomic_store_n(&fault, SERVICE_STALL, __ATOMIC_RELEASE);
  __atomic_store_n(&disabled, true, __ATOMIC_RELEASE);
  return true;
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

static void __not_in_flash_func(service_pass)() {
  uint64_t entered = z80_time_us_64();
  // Core 0 is locked out for the whole flash erase/program of a disk flush.
  // The transfer kept running without it, so that time is not the device's.
  if (state != State::idle && last_service &&
      entered - last_service > MAX_SERVICE_GAP_US)
    deadline += entered - last_service;
  last_service = entered;
  if (disabled) {
    if (!reported) {
      reported = true;
      printf("[speaker] FAIL: bridge service stalled (state=%lu backlog=%lu "
             "i2c_busy=%d); sound disabled until reboot.\n",
             (unsigned long)stall_state, (unsigned long)stall_backlog,
             display_i2c::busy());
    }
    // Finish a transfer left in flight so the shared OLED bus is released.
    if (state != State::idle && display_i2c::busy() &&
        display_i2c::poll() == display_i2c::Result::pending)
      return;
    state = State::idle;
    return;
  }
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
    uint64_t now = z80_time_us_64();
    if (now < next_at)
      return;
    deadline = now + 50000;
    if (request.size) {
      if (start_write(request))
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
  if (result != display_i2c::Result::complete &&
      result != display_i2c::Result::nack) {
    fail(BUS_FAILURE);
    return;
  }
  uint64_t now = z80_time_us_64();
  if (result == display_i2c::Result::nack) {
    // Nothing is retried: a refused command is lost, a refused read is invalid.
    __atomic_fetch_add(&dropped, 1u, __ATOMIC_RELAXED);
    if (!request.size) {
      memset(status, 0, sizeof(status));
      status[S_RESULT] = RESULT_INVALID;
    }
  } else {
    __atomic_fetch_add(&transfers, 1u, __ATOMIC_RELAXED);
    if (!request.size) {
      if (fetched[S_VERSION] != PROTOCOL_VERSION) {
        fail(PROTOCOL_MISMATCH);
        return;
      }
      // Wait for a command still executing so the status is current. A device
      // that stays busy past the deadline reports its status as it is.
      if ((fetched[S_FLAGS] & STATUS_COMMAND_PENDING) && now < deadline) {
        retry_at = now + STATUS_POLL_US;
        state = State::execute;
        return;
      }
      memcpy(status, fetched, sizeof(status));
    }
  }
  next_at = now + WRITE_GAP_US;
  state = State::idle;
  __atomic_store_n(&consumed, tail + 1, __ATOMIC_RELEASE);
}

// Core 0 calls this every pass. An idle bridge with an empty queue returns
// before any timer read.
extern "C" void __not_in_flash_func(z80_speaker_service)(void) {
  if (state == State::idle &&
      (disabled ? reported
                : !__atomic_load_n(&fault, __ATOMIC_RELAXED) &&
                      consumed == __atomic_load_n(&produced, __ATOMIC_ACQUIRE)))
    return;
  service_pass();
}

extern "C" bool z80_speaker_quiescent(void) {
  return disabled || (state == State::idle &&
         consumed == __atomic_load_n(&produced, __ATOMIC_ACQUIRE));
}

extern "C" void z80_speaker_print_status(void) {
  printf("[speaker] address=42h baud=%lu "
         "fault=%lu transfers=%lu dropped=%lu\n",
         (unsigned long)display_i2c::BAUD,
         (unsigned long)__atomic_load_n(&fault, __ATOMIC_ACQUIRE),
         (unsigned long)__atomic_load_n(&transfers, __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&dropped, __ATOMIC_RELAXED));
}
