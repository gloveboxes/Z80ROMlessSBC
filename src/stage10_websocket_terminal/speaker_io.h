#ifndef Z80_STAGE10_SPEAKER_IO_H
#define Z80_STAGE10_SPEAKER_IO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Z80_SPEAKER_PORT 0x15

/* Core 0, including PIO IRQ: bounded packet collection and snapshot reads. */
void z80_speaker_out(uint8_t value);
uint8_t z80_speaker_in(void);
/* Read gate for Z80_SPEAKER_PORT: false while a queued request is unfinished. */
bool z80_speaker_read_ready(void);
/* Core 0 main loop and bus-grant wait: nonblocking shared-bus service. */
void z80_speaker_service(void);
bool z80_speaker_quiescent(void);
void z80_speaker_print_status(void);

#ifdef __cplusplus
}
#endif
#endif
