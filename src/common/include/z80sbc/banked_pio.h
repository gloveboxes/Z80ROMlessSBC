#ifndef Z80SBC_BANKED_PIO_H
#define Z80SBC_BANKED_PIO_H

#include <stdbool.h>
#include <stdint.h>

/* Core 0 only. PIO1 owns IRQ flags 0..2 and two state machines while allocated.
 * Banked runtime requires the 120 MHz supervisor / 20 MHz Z80 clock pair.
 * Resource or startup qualification failure disables runtime banking; other I/O
 * remains available through the software trap.
 */
bool z80_banked_pio_rearm(void (*slow_handler)(void));
void z80_banked_pio_disable(void);
void z80_banked_pio_slow_complete(void);
bool z80_banked_pio_active(void);
bool z80_banked_pio_clock_owned(void);
bool z80_banked_pio_clock_start(uint32_t hz);
void z80_banked_pio_clock_stop(void);
void z80_banked_pio_clock_resume(void);
bool z80_banked_pio_poll(void);
typedef struct {
  uint32_t requests, reads, writes, pins;
  unsigned clock_pc, io_pc, irqs;
  uint8_t port, value;
  bool active, slow, running, read;
} z80_banked_pio_status_t;
void z80_banked_pio_note_io(uint8_t port, bool read, uint8_t value);
z80_banked_pio_status_t z80_banked_pio_status(void);

#endif
