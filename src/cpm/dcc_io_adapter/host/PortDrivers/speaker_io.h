#pragma once

#include <stdint.h>

/* Port 0x15 speaker: the Stage 11 ATtiny85 sound engine on the host.
 *
 * OUT bytes are ATtiny85 command packets, framed by the packet's own length.
 * At a packet boundary, OUT 0x00 latches the 19-byte status, which successive
 * INs return (0xFF after the last byte). An IN also discards a partial packet
 * and ends a framing-error discard, so "IN, OUT 0x00" resynchronizes.
 */
#define SPEAKER_IO_PORT 0x15u
#define SPEAKER_IO_LATCH_STATUS 0x00u
#define SPEAKER_IO_SAMPLE_RATE 48000u

typedef enum speaker_io_clock
{
    SPEAKER_IO_CLOCK_AUDIO,  /* audio callback; wall clock if unavailable */
    SPEAKER_IO_CLOCK_WALL,   /* monotonic wall clock, silent */
    SPEAKER_IO_CLOCK_MANUAL  /* only speaker_io_render() advances time */
} speaker_io_clock_t;

void speaker_io_init(speaker_io_clock_t clock);
void speaker_io_close(void);
void speaker_io_output(uint8_t data);
uint8_t speaker_io_input(void);

/* Advances the engine one millisecond per SPEAKER_IO_SAMPLE_RATE/1000 frames
 * and writes mono float samples. The audio callback calls it under the lock. */
void speaker_io_render(float *output, uint32_t frames);
