#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../io_service.h"

static size_t copied_bytes;
static void *counted_memcpy(void *dst, const void *src, size_t length) {
  copied_bytes += length;
  return memcpy(dst, src, length);
}

#undef memcpy
#define memcpy counted_memcpy
#include "../io_ports.c"
#undef memcpy

static io_service_result_t worker_result;
static bool pending, done;
static uint32_t submitted_generation;

void z80_io_service_init(void) {
  pending = done = false;
}

uint64_t z80_service_millis(void) { return 0; }

io_service_error_t z80_io_service_submit(uint8_t command, const uint8_t *data,
                                        size_t length, uint32_t generation) {
  assert(command == IO_SERVICE_UTILITY && length == 1 && data[0] == 0);
  if (pending || done)
    return IO_SERVICE_BUSY_ERROR;
  submitted_generation = generation;
  pending = true;
  return IO_SERVICE_OK;
}

const io_service_result_t *z80_io_service_poll(void) {
  if (!done)
    return NULL;
  done = false;
  return &worker_result;
}

static void submit(void) {
  z80_io_port_out(2, IO_SERVICE_UTILITY);
  z80_io_port_out(3, 1);
  z80_io_port_out(3, 0);
  z80_io_port_out(3, 0);
  assert(z80_io_port_in(2) == IO_SERVICE_BUSY);
}

static void complete(size_t length, io_service_error_t error) {
  assert(pending && !done);
  pending = false;
  worker_result.generation = submitted_generation;
  worker_result.error = error;
  worker_result.has_response = true;
  worker_result.length = length;
  for (size_t i = 0; i < sizeof(worker_result.data); ++i)
    worker_result.data[i] = (uint8_t)(i * 13 + 7);
  done = true;
}

static void test_response(size_t length) {
  z80_io_ports_init();
  submit();
  complete(length, IO_SERVICE_OK);
  copied_bytes = 0;
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  assert(copied_bytes == 0);
  assert(z80_io_port_in(4) == (uint8_t)length);
  assert(z80_io_port_in(4) == (uint8_t)(length >> 8));
  for (size_t i = 0; i < length; ++i) {
    assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(z80_io_port_in(4) == (uint8_t)(i * 13 + 7));
  }
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  assert(copied_bytes == 0);
  submit();
  complete(1, IO_SERVICE_OK);
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  assert(z80_io_port_in(4) == 1 && z80_io_port_in(4) == 0);
  assert(z80_io_port_in(4) == 7);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
}

static void test_reset_and_errors(void) {
  z80_io_ports_init();
  submit();
  complete(IO_SERVICE_MAX_PAYLOAD, IO_SERVICE_OK);
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  assert(z80_io_port_in(4) == 0);
  z80_io_port_out(2, IO_SERVICE_RESET);
  z80_io_port_out(2, IO_SERVICE_IDENTIFY);
  assert(z80_io_port_in(4) == 13 && z80_io_port_in(4) == 0);
  for (size_t i = 0; i < 13; ++i)
    assert(z80_io_port_in(4) == (uint8_t)"ROMLESS-SVC/1"[i]);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);

  submit();
  z80_io_port_out(2, IO_SERVICE_RESET);
  complete(100, IO_SERVICE_OK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
  submit();
  complete(1, IO_SERVICE_UNAVAILABLE);
  assert(z80_io_port_in(2) == IO_SERVICE_ERROR);
  assert(z80_io_port_in(4) == 1 && z80_io_port_in(4) == 0);
  assert(z80_io_port_in(4) == IO_SERVICE_UNAVAILABLE);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);

  submit();
  complete(IO_SERVICE_MAX_PAYLOAD + 1, IO_SERVICE_OK);
  assert(z80_io_port_in(2) == IO_SERVICE_ERROR);
  assert(z80_io_port_in(4) == 1 && z80_io_port_in(4) == 0);
  assert(z80_io_port_in(4) == IO_SERVICE_CAPACITY);
  z80_io_port_out(2, IO_SERVICE_ACK);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);

  submit();
  complete(16, IO_SERVICE_OK);
  assert(z80_io_port_in(2) == IO_SERVICE_RESPONSE_READY);
  z80_io_port_out(2, IO_SERVICE_IDENTIFY);
  assert(z80_io_port_in(2) == IO_SERVICE_ERROR);
  assert(z80_io_port_in(4) == 1 && z80_io_port_in(4) == 0);
  assert(z80_io_port_in(4) == IO_SERVICE_BUSY_ERROR);
  z80_io_port_out(2, IO_SERVICE_RESET);
  assert(z80_io_port_in(2) == IO_SERVICE_READY);
}

int main(void) {
  test_response(0);
  test_response(1);
  test_response(16);
  test_response(IO_SERVICE_MAX_PAYLOAD);
  test_reset_and_errors();
  puts("PASS: response handoff copies zero payload bytes; framing, reset and errors preserved");
}
