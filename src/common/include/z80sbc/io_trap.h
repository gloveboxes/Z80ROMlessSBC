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
/* Z80_BANKED_PIO builds route every cycle through PIO1 and fail (returning
 * false) when PIO1 cannot be allocated; other builds use the IORQ GPIO IRQ. */
bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler,
                        void *context);
bool z80_io_trap_rearm(void);
/* Stops servicing and applies every OUT the trap has already completed. */
void z80_io_trap_disable(void);
/* Applies completed OUTs without stopping service. PIO builds complete OUTs
 * ahead of core 0; call with the Z80 parked (BUSACK) before relying on all
 * prior OUTs having taken effect. Other builds apply OUTs synchronously. */
void z80_io_trap_drain_writes(void);
/* Core 0 service loop: retains a 500 ms fail-closed timeout for PIO cycles. */
void z80_io_trap_poll(void);
uint32_t z80_io_trap_timeout_count(void);
uint32_t z80_io_trap_control_error_count(void);
#if !defined(Z80_BANKED_PIO)
/* GPIO trap only: IORQ# edges that had already released when serviced. */
uint32_t z80_io_trap_inactive_irq_count(void);
#endif

#endif
