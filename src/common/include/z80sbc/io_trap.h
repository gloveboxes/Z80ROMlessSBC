#ifndef Z80SBC_IO_TRAP_H
#define Z80SBC_IO_TRAP_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t (*z80_io_read_handler_t)(uint8_t port, void *context);
typedef void (*z80_io_write_handler_t)(uint8_t port, uint8_t value,
                                       void *context);

bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler,
                        void *context);
void z80_io_trap_rearm(void);
void z80_io_trap_disable(void);
uint32_t z80_io_trap_timeout_count(void);
uint32_t z80_io_trap_control_error_count(void);

#endif