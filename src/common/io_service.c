#include "io_service.h"

#include <stdio.h>
#include <string.h>

#include "z80sbc/flash_disk.h"
#include "flash_backend.h"
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

enum { WORK_IDLE, WORK_PENDING, WORK_DONE };

static struct {
  uint32_t generation;
  uint8_t command;
  size_t length;
  uint8_t data[IO_SERVICE_MAX_PAYLOAD];
} request;
static io_service_result_t result;
static uint32_t work_state;
static uint32_t reboot_requested;

void z80_io_service_init(void) {
  __atomic_store_n(&work_state, WORK_IDLE, __ATOMIC_RELEASE);
  __atomic_store_n(&reboot_requested, 0, __ATOMIC_RELEASE);
}

io_service_error_t z80_io_service_submit(uint8_t command, const uint8_t *data,
                                        size_t length, uint32_t generation) {
  if (__atomic_load_n(&work_state, __ATOMIC_ACQUIRE) != WORK_IDLE)
    return IO_SERVICE_BUSY_ERROR;
  request.command = command;
  request.length = length;
  request.generation = generation;
  memcpy(request.data, data, length);
  if (command == IO_SERVICE_UTILITY && length == 2 &&
      data[0] == 2 && data[1] == 0xa5)
    z80_flash_disk_set_write_enabled(false);
  __atomic_store_n(&work_state, WORK_PENDING, __ATOMIC_RELEASE);
  return IO_SERVICE_OK;
}

const io_service_result_t *z80_io_service_poll(void) {
  if (__atomic_load_n(&work_state, __ATOMIC_ACQUIRE) != WORK_DONE)
    return NULL;
  /* Only core 0 submits the next job, after it has copied this result. */
  __atomic_store_n(&work_state, WORK_IDLE, __ATOMIC_RELEASE);
  return &result;
}

bool z80_io_ports_quiescent(void) {
  return __atomic_load_n(&work_state, __ATOMIC_ACQUIRE) != WORK_PENDING;
}

bool z80_io_ports_reboot_requested(void) {
  return __atomic_exchange_n(&reboot_requested, 0, __ATOMIC_ACQ_REL) != 0;
}

static io_service_error_t env_status(uint8_t status) {
  switch (status) {
  case 0: return IO_SERVICE_OK;
  case 255: return IO_SERVICE_STORAGE_OPEN;
  case 254: return IO_SERVICE_STORAGE_READ;
  case 253: return IO_SERVICE_STORAGE_WRITE;
  case 252: return IO_SERVICE_CAPACITY;
  case 251: return IO_SERVICE_NOT_FOUND;
  default: return IO_SERVICE_UNAVAILABLE;
  }
}

static io_service_error_t environment_service(void) {
  uint8_t operation = request.data[0];
  bool has_key = operation == 2 || operation == 3 ||
                 operation == 4 || operation == 8;
  size_t key_length = 0;
  size_t value_length = 0;
  if (operation > 9)
    return IO_SERVICE_UNKNOWN_OPERATION;
  if (has_key) {
    if (request.length < 2)
      return IO_SERVICE_INVALID_LENGTH;
    key_length = request.data[1];
    if (key_length == 0 || key_length > 16)
      return IO_SERVICE_INVALID_PARAMETER;
    if (request.length < 2 + key_length)
      return IO_SERVICE_INVALID_LENGTH;
    if (memchr(request.data + 2, 0, key_length))
      return IO_SERVICE_INVALID_PARAMETER;
    if (operation == 3) {
      if (request.length < 4 + key_length)
        return IO_SERVICE_INVALID_LENGTH;
      value_length = request.data[2 + key_length] |
                     ((uint16_t)request.data[3 + key_length] << 8);
      if (value_length > 255)
        return IO_SERVICE_INVALID_PARAMETER;
      if (request.length != 4 + key_length + value_length)
        return IO_SERVICE_INVALID_LENGTH;
      if (memchr(request.data + 4 + key_length, 0, value_length))
        return IO_SERVICE_INVALID_PARAMETER;
    } else if (request.length != 2 + key_length) {
      return IO_SERVICE_INVALID_LENGTH;
    }
  } else if (operation == 9) {
    if (request.length > 512)
      return IO_SERVICE_INVALID_LENGTH;
    if (memchr(request.data + 1, 0, request.length - 1))
      return IO_SERVICE_INVALID_PARAMETER;
  } else if (request.length != 1) {
    return IO_SERVICE_INVALID_LENGTH;
  }

  /* Reload the durable snapshot for each request, including after failures. */
  environment_io_close();
  environment_io_init(NULL);
  io_service_error_t error =
      env_status(environment_input(ENVIRONMENT_PORT_COMMAND));
  if (error != IO_SERVICE_OK)
    return error;
  environment_output(ENVIRONMENT_PORT_COMMAND, 0, NULL, 0);
  if (has_key) {
    for (size_t i = 0; i < key_length; ++i)
      environment_output(ENVIRONMENT_PORT_DATA, request.data[2 + i], NULL, 0);
    environment_output(ENVIRONMENT_PORT_DATA, 0, NULL, 0);
    if (operation == 3)
      for (size_t i = 0; i < value_length; ++i)
        environment_output(ENVIRONMENT_PORT_DATA,
                           request.data[4 + key_length + i], NULL, 0);
  } else if (operation == 9) {
    for (size_t i = 1; i < request.length; ++i)
      environment_output(ENVIRONMENT_PORT_DATA, request.data[i], NULL, 0);
  }
  result.length = environment_output(ENVIRONMENT_PORT_COMMAND, operation,
                                     (char *)result.data, sizeof(result.data));
  result.has_response =
      operation == 2 || operation == 5 || operation == 6 || operation == 9;
  return env_status(environment_input(ENVIRONMENT_PORT_COMMAND));
}

static io_service_error_t clock_service(void) {
  uint8_t operation = request.data[0];
  if (operation > 4)
    return IO_SERVICE_UNKNOWN_OPERATION;
  if (request.length != 1)
    return IO_SERVICE_INVALID_LENGTH;
  if (operation == 0) {
    int length = snprintf((char *)result.data, sizeof(result.data), "%llu",
                         (unsigned long long)(z80_service_millis() / 1000));
    if (length < 0 || (size_t)length >= sizeof(result.data))
      return IO_SERVICE_CAPACITY;
  } else {
    environment_io_close();
    environment_io_init(NULL);
    io_service_error_t error =
        env_status(environment_input(ENVIRONMENT_PORT_COMMAND));
    if (error != IO_SERVICE_OK)
      return error;
    if (!z80_service_clock(operation, (char *)result.data, sizeof(result.data)))
      return IO_SERVICE_CLOCK_UNSYNCED;
  }
  result.length = strlen((const char *)result.data);
  result.has_response = true;
  return IO_SERVICE_OK;
}

static io_service_error_t utility_service(void) {
  uint8_t operation = request.data[0];
  if (operation > 3)
    return IO_SERVICE_UNKNOWN_OPERATION;
  if (request.length != ((operation == 1 || operation == 2) ? 2u : 1u))
    return IO_SERVICE_INVALID_LENGTH;
  if (operation == 0) {
    uint16_t random = z80_service_random();
    result.data[0] = (uint8_t)random;
    result.data[1] = (uint8_t)(random >> 8);
    result.length = 2;
  } else if (operation == 1) {
    if (request.data[1] > 2)
      return IO_SERVICE_INVALID_PARAMETER;
    result.length = z80_service_info(request.data[1], (char *)result.data,
                                     sizeof(result.data));
    if (result.length == 0)
      return IO_SERVICE_UNAVAILABLE;
  } else if (operation == 2) {
    if (request.data[1] != 0xa5)
      return IO_SERVICE_REBOOT_CONFIRMATION;
    /* Flush on the worker; core 0 keeps servicing the BUSACK handshake. */
    z80_flash_core1_service();
    z80_flash_core1_service();
    if (!z80_flash_backend_flush() || !z80_flash_disk_quiescent()) {
      z80_flash_disk_set_write_enabled(true);
      return IO_SERVICE_STORAGE_WRITE;
    }
    __atomic_store_n(&reboot_requested, 1, __ATOMIC_RELEASE);
    return IO_SERVICE_OK;
  } else {
    static const char version[] = "Pico 2 W Z80ROMlessSBC ROMLESS-SVC/1\n";
    memcpy(result.data, version, sizeof(version));
    result.length = sizeof(version) - 1;
  }
  result.has_response = true;
  return IO_SERVICE_OK;
}

void z80_io_ports_core1_service(void) {
  if (__atomic_load_n(&work_state, __ATOMIC_ACQUIRE) != WORK_PENDING)
    return;
  memset(&result, 0, sizeof(result));
  result.generation = request.generation;
  switch (request.command) {
  case IO_SERVICE_CLOCK:
    result.error = clock_service();
    break;
  case IO_SERVICE_UTILITY:
    result.error = utility_service();
    break;
  case IO_SERVICE_ENVIRONMENT:
    result.error = environment_service();
    break;
  default:
    result.error = IO_SERVICE_UNAVAILABLE;
    break;
  }
  __atomic_store_n(&work_state, WORK_DONE, __ATOMIC_RELEASE);
}
