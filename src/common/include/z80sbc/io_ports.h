#ifndef Z80SBC_IO_PORTS_H
#define Z80SBC_IO_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  IO_SERVICE_COMMAND_PORT = 0x02,
  IO_SERVICE_REQUEST_PORT = 0x03,
  IO_SERVICE_RESPONSE_PORT = 0x04,
  IO_TIMER_PORT = 0x17,
  IO_SERVICE_MAX_PAYLOAD = 2048,
  IO_SERVICE_ACK = 0x00,
  IO_SERVICE_RESET = 0x01,
  IO_SERVICE_IDENTIFY = 0x02,
  IO_SERVICE_TIMERS = 0x10,
  IO_SERVICE_STOPWATCHES = 0x11,
  IO_SERVICE_CLOCK = 0x20,
  IO_SERVICE_UTILITY = 0x21,
  IO_SERVICE_ENVIRONMENT = 0x30,
  IO_SERVICE_WEATHER = 0x40,
  IO_SERVICE_CHAT = 0x50,
  IO_SERVICE_SENSE_HAT = 0x60,
};

typedef enum {
  IO_SERVICE_READY = 0,
  IO_SERVICE_COMPLETE = 1,
  IO_SERVICE_RESPONSE_READY = 2,
  IO_SERVICE_COLLECTING = 3,
  IO_SERVICE_BUSY = 4,
  IO_SERVICE_ERROR = 0xff,
} io_service_status_t;

typedef enum {
  IO_SERVICE_OK = 0,
  IO_SERVICE_UNKNOWN_GROUP = 1,
  IO_SERVICE_UNKNOWN_OPERATION = 2,
  IO_SERVICE_INVALID_LENGTH = 3,
  IO_SERVICE_INVALID_PARAMETER = 4,
  IO_SERVICE_UNAVAILABLE = 5,
  IO_SERVICE_BUSY_ERROR = 6,
  IO_SERVICE_TIMEOUT = 7,
  IO_SERVICE_INVALID_STATE = 8,
  IO_SERVICE_UNDERFLOW = 9,
  IO_SERVICE_STORAGE_OPEN = 0x10,
  IO_SERVICE_STORAGE_READ = 0x11,
  IO_SERVICE_STORAGE_WRITE = 0x12,
  IO_SERVICE_CAPACITY = 0x13,
  IO_SERVICE_NOT_FOUND = 0x14,
  IO_SERVICE_CLOCK_UNSYNCED = 0x15,
  IO_SERVICE_REBOOT_CONFIRMATION = 0x16,
} io_service_error_t;

/* Initialize on core 0 before launching core 1. Port handlers run on core 0. */
void z80_io_ports_init(void);
bool z80_io_ports_handles(uint8_t port);
uint8_t z80_io_port_in(uint8_t port);
void z80_io_port_out(uint8_t port, uint8_t data);
/* Run after disk service on core 1, including while draining for a pause. */
void z80_io_ports_core1_service(void);
bool z80_io_ports_quiescent(void);
/* Core 0 consumes this only after the worker has flushed disks. */
bool z80_io_ports_reboot_requested(void);
/* Core 1 network hooks. The SNTP callback may run in a core 1 interrupt. */
void z80_io_ports_set_unix_time(uint32_t seconds);
void z80_io_ports_set_network_address(const char *address);
/* Thread-safe snapshot for supervisor diagnostics; no network-stack access. */
size_t z80_io_ports_get_network_address(char *address, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
