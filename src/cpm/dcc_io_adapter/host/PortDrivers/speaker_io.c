/* Port 0x15 speaker: compiles the Stage 11 ATtiny85 firmware unchanged over
 * the host AVR register model, then renders its Timer1 OC1A pin as audio. */
#include "PortDrivers/speaker_io.h"
#include "PortDrivers/speaker_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define __atomic_signal_fence(order) ((void)0)
#endif
#define main attiny85_firmware_main
#include "target/main.c"
#undef main

#ifdef _WIN32
#include <windows.h>
static uint64_t monotonic_ms(void) { return GetTickCount64(); }
#else
#include <time.h>
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}
#endif

#define SAMPLE_SPAN F_CPU /* one sample in timer-tick * sample-rate units */
#define AMPLITUDE 0.2f
#define WALL_CATCH_UP_MS 60000u

static speaker_io_clock_t s_clock;
static int s_audio_tried, s_discarding;
static uint8_t s_cursor;
static uint64_t s_wall_ms;
static uint32_t s_frames_left, s_frame_remainder;
/* Waveform in timer ticks scaled by the sample rate; s_period 0 is silence. */
static uint64_t s_period, s_high, s_phase;
static uint8_t s_tccr1, s_ocr1c, s_ocr1a;

static void reset_engine(void)
{
    memset(&sound, 0, sizeof(sound));
    playback_count = event_head = event_count = event_overflow = 0;
    event_sequence = completed_count = rejected_count = 0;
    request_id = request_result = 0;
    command_pending = start_pending = packet_size = expected_size = 0;
    milliseconds = serviced_ms = 0;
    tone_stop();
    publish_status();
    memcpy(transmit, published, STATUS_BYTES);
    s_cursor = STATUS_BYTES;
    s_discarding = 0;
}

static void tick_ms(void)
{
    TIM0_COMPA_vect();
    service_effects();
}

static void load_waveform(void)
{
    uint64_t period, high;

    s_tccr1 = TCCR1;
    s_ocr1c = OCR1C;
    s_ocr1a = OCR1A;
    if (!(TCCR1 & 15u)) {
        s_period = s_phase = 0;
        return;
    }
    /* PWM1A: high from BOTTOM to OCR1A. CTC1 toggle: half of two periods. */
    period = ((uint64_t)OCR1C + 1u) << ((TCCR1 & 15u) - 1u);
    high = TCCR1 & _BV(PWM1A) ? (uint64_t)OCR1A << ((TCCR1 & 15u) - 1u) : period;
    if (!(TCCR1 & _BV(PWM1A)))
        period *= 2u;
    if (high > period)
        high = period;
    if (!s_period)
        s_phase = 0;
    s_period = period * SPEAKER_IO_SAMPLE_RATE;
    s_high = high * SPEAKER_IO_SAMPLE_RATE;
    s_phase %= s_period;
}

/* Box-filters the pin over each sample: exact high time, no aliasing spikes. */
static void synthesize(float *output, uint32_t frames)
{
    if (TCCR1 != s_tccr1 || OCR1C != s_ocr1c || OCR1A != s_ocr1a)
        load_waveform();
    if (!s_period) {
        memset(output, 0, frames * sizeof(*output));
        return;
    }
    for (uint32_t i = 0; i < frames; ++i) {
        uint64_t end = s_phase + SAMPLE_SPAN;
        uint64_t start_high = s_phase < s_high ? s_phase : s_high;
        uint64_t wraps = end / s_period, rest = end % s_period;
        uint64_t end_high = wraps * s_high + (rest < s_high ? rest : s_high);
        float duty = (float)(end_high - start_high) * (1.0f / (float)SAMPLE_SPAN);
        output[i] = AMPLITUDE * (2.0f * duty - 1.0f);
        s_phase = rest;
    }
}

void speaker_io_render(float *output, uint32_t frames)
{
    while (frames) {
        if (!s_frames_left) {
            uint32_t total = s_frame_remainder + SPEAKER_IO_SAMPLE_RATE;
            tick_ms();
            s_frames_left = total / 1000u;
            s_frame_remainder = total % 1000u;
        }
        uint32_t count = frames < s_frames_left ? frames : s_frames_left;
        synthesize(output, count);
        output += count;
        frames -= count;
        s_frames_left -= count;
    }
}

static void begin_access(void)
{
    if (s_clock == SPEAKER_IO_CLOCK_AUDIO && !s_audio_tried) {
        s_audio_tried = 1;
        if (!speaker_audio_open(SPEAKER_IO_SAMPLE_RATE, speaker_io_render)) {
            fprintf(stderr, "speaker: audio output unavailable; port 0x15 is silent\n");
            s_clock = SPEAKER_IO_CLOCK_WALL;
            s_wall_ms = monotonic_ms();
        }
    }
    speaker_audio_lock();
    if (s_clock == SPEAKER_IO_CLOCK_WALL) {
        uint64_t now = monotonic_ms();
        uint64_t elapsed = now - s_wall_ms;
        s_wall_ms = now;
        if (elapsed > WALL_CATCH_UP_MS)
            elapsed = WALL_CATCH_UP_MS;
        while (elapsed--)
            tick_ms();
    }
}

void speaker_io_init(speaker_io_clock_t clock)
{
    const char *setting = getenv("Z80SBC_SPEAKER");

    if (clock == SPEAKER_IO_CLOCK_AUDIO && setting && strcmp(setting, "off") == 0)
        clock = SPEAKER_IO_CLOCK_WALL;
    s_clock = clock;
    s_audio_tried = 0;
    s_wall_ms = monotonic_ms();
    s_frames_left = s_frame_remainder = 0;
    reset_engine();
    load_waveform();
}

void speaker_io_close(void)
{
    speaker_audio_close();
    reset_engine();
    load_waveform();
}

void speaker_io_output(uint8_t data)
{
    begin_access();
    if (s_discarding) {
        /* Framing is lost; ignore bytes until an IN, like a NACK until START. */
    } else if (!packet_size && data == SPEAKER_IO_LATCH_STATUS) {
        publish_status();
        memcpy(transmit, published, STATUS_BYTES);
        s_cursor = 0;
    } else if (accept_byte(data)) {
        if (command_pending) {
            service_command();
            packet_size = expected_size = 0;
        }
    } else {
        /* A complete invalid packet is dropped with framing intact. */
        s_discarding = packet_length(packet) != packet_size;
        packet_size = expected_size = 0;
    }
    speaker_audio_unlock();
}

uint8_t speaker_io_input(void)
{
    uint8_t value;

    begin_access();
    packet_size = expected_size = 0;
    s_discarding = 0;
    value = s_cursor < STATUS_BYTES ? transmit[s_cursor++] : 0xffu;
    speaker_audio_unlock();
    return value;
}
