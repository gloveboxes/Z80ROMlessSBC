#ifndef Z80SBC_BANKED_PIO_H
#define Z80SBC_BANKED_PIO_H

#include <stdbool.h>
#include <stdint.h>

/* Core 0 only. A PIO0 state machine runs the 20 MHz Z80 clock (PIO0 IRQ flags
 * 1..2, shared with the matrix display) and PIO1 runs the whole I/O engine,
 * claimed exclusively; PIO2 is left to the CYW43 SPI driver. Requires the
 * 120 MHz supervisor clock. Rearm fails if either cannot be allocated;
 * GP8/A16 is handed to PIO only when bank 1 has been verified.
 *
 * Every Z80 cycle completes in PIO. OUT 07h drives A16; other OUTs are queued
 * and applied through write_handler. An IN waits, with WAIT held, for
 * read_handler's value, which PIO drives onto the bus. Both run from the PIO1
 * IRQ in bus order. fault_handler receives a GPIO snapshot of a cycle with
 * neither or both strobes active and must not return. */
bool z80_banked_pio_rearm(uint8_t (*read_handler)(uint8_t port),
                          void (*write_handler)(uint8_t port, uint8_t value),
                          void (*fault_handler)(uint32_t controls));
/* Service queued cycles now; for paths that must observe every completed OUT. */
void z80_banked_pio_drain(void);
void z80_banked_pio_disable(void);
bool z80_banked_pio_clock_start(uint32_t hz);
void z80_banked_pio_clock_stop(void);
void z80_banked_pio_clock_resume(void);
bool z80_banked_pio_poll(void);
typedef struct {
  uint32_t reads, writes, pins;
  unsigned clock_pc, io_pc, irqs;
  uint8_t port, value;
  bool active, running, read;
} z80_banked_pio_status_t;
void z80_banked_pio_note_io(uint8_t port, bool read, uint8_t value);
/* Core-0 service cost in clk_sys cycles since the previous call (which
 * resets the window): per IN from taking the request to queuing the reply,
 * per OUT to apply it. PIO-to-IRQ entry latency and the Z80's own wait
 * states are not included. */
typedef struct {
  bool available;
  uint32_t in_count, in_max_cycles, out_count, out_max_cycles;
  uint64_t in_cycles, out_cycles;
} z80_banked_pio_latency_t;
z80_banked_pio_latency_t z80_banked_pio_take_latency(void);
z80_banked_pio_status_t z80_banked_pio_status(void);

#endif
