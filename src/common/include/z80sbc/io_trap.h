#ifndef Z80SBC_IO_TRAP_H
#define Z80SBC_IO_TRAP_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t (*z80_io_read_handler_t)(uint8_t port, void *context);
typedef void (*z80_io_write_handler_t)(uint8_t port, uint8_t value,
                                       void *context);

typedef enum {
  Z80_IO_TRAP_RESET_NONE,
  Z80_IO_TRAP_RESET_RELEASE_TIMEOUT,
  Z80_IO_TRAP_RESET_INVALID_CONTROLS,
} z80_io_trap_reset_reason_t;

typedef struct {
  z80_io_trap_reset_reason_t reason;
  uint32_t gpio;
} z80_io_trap_reset_fault_t;

z80_io_trap_reset_fault_t z80_io_trap_take_reset_fault(void);
bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler,
                        void *context);
void z80_io_trap_rearm(void);
void z80_io_trap_disable(void);
uint32_t z80_io_trap_timeout_count(void);
uint32_t z80_io_trap_control_error_count(void);
uint32_t z80_io_trap_inactive_irq_count(void);

#endif