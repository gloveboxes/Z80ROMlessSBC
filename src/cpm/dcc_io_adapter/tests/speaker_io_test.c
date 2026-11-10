#ifdef NDEBUG
#undef NDEBUG
#endif
#include "PortDrivers/speaker_io.h"
#include "target/protocol.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#define BE16(value) (uint8_t)((value) >> 8), (uint8_t)(value)
#define SEND(...) send((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))
#define FRAMES_PER_MS (SPEAKER_IO_SAMPLE_RATE / 1000u)

static float samples[SPEAKER_IO_SAMPLE_RATE];

static void send(const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        speaker_io_output(bytes[i]);
}

static void status(uint8_t *bytes)
{
    speaker_io_output(SPEAKER_IO_LATCH_STATUS);
    for (unsigned i = 0; i < STATUS_BYTES; ++i)
        bytes[i] = speaker_io_input();
    assert(speaker_io_input() == 0xff);
}

static unsigned cycles(unsigned ms)
{
    unsigned crossings = 0;
    speaker_io_render(samples, ms * FRAMES_PER_MS);
    for (unsigned i = 1; i < ms * FRAMES_PER_MS; ++i)
        crossings += samples[i - 1] < 0.0f && samples[i] >= 0.0f;
    return crossings;
}

static int silent(unsigned ms)
{
    speaker_io_render(samples, ms * FRAMES_PER_MS);
    for (unsigned i = 0; i < ms * FRAMES_PER_MS; ++i)
        if (samples[i] != 0.0f)
            return 0;
    return 1;
}

static void acknowledge(uint8_t id, uint8_t type)
{
    uint8_t s[STATUS_BYTES];
    status(s);
    assert(s[S_EVENT_ID] == id && s[S_EVENT_TYPE] == type);
    SEND(COMMAND_ACK_EVENT, s[S_EVENT_SEQ_HI], s[S_EVENT_SEQ_LO]);
}

static void test_status_and_tone(void)
{
    uint8_t s[STATUS_BYTES];

    speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
    status(s);
    assert(s[S_VERSION] == PROTOCOL_VERSION && s[S_FLAGS] == 0);
    assert(silent(5));
    SEND(COMMAND_PLAY, 1, 0, 0, 100, 100, FX_TONE, BE16(1000), BE16(100));
    status(s);
    assert(s[S_FLAGS] == STATUS_PLAYING && s[S_ACTIVE_ID] == 1);
    assert(s[S_EFFECT] == FX_TONE && s[S_REQUEST_ID] == 1);
    assert(s[S_RESULT] == RESULT_ACCEPTED);
    assert(read_be16(s + S_FREQUENCY_HI) == 1000);
    unsigned count = cycles(50);
    assert(count >= 49 && count <= 51);
    speaker_io_render(samples, 60 * FRAMES_PER_MS);
    status(s);
    assert(!(s[S_FLAGS] & STATUS_PLAYING) && read_be16(s + S_COMPLETED_HI) == 1);
    assert(silent(10));
    acknowledge(1, EVENT_STARTED);
    acknowledge(1, EVENT_COMPLETED);
    status(s);
    assert(!s[S_EVENT_TYPE]);
    speaker_io_close();
}

static void test_pitch_accuracy(void)
{
    static const uint16_t tones[] = {55, 262, 440, 1047, 4186, 10000};

    speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
    for (unsigned i = 0; i < sizeof(tones) / sizeof(tones[0]); ++i) {
        SEND(COMMAND_PLAY, 2, 0, 0, 100, 100, FX_TONE, BE16(tones[i]), BE16(2000));
        double measured = cycles(1000);
        assert(fabs(measured - tones[i]) <= tones[i] * 0.01 + 1.0);
    }
    SEND(COMMAND_STOP, 3);
    assert(silent(2));
    speaker_io_close();
}

static void test_sweep_follows_engine(void)
{
    speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
    SEND(COMMAND_PLAY, 4, 0, 0, 100, 100, FX_SWEEP, BE16(1000), BE16(3000), BE16(1000));
    unsigned first = cycles(100), last;
    speaker_io_render(samples, 800 * FRAMES_PER_MS);
    last = cycles(100);
    assert(first >= 105 && first <= 115);   /* mean 1100 Hz */
    assert(last >= 285 && last <= 295);     /* mean 2900 Hz */
    assert(silent(5));
    speaker_io_close();
}

static void test_framing(void)
{
    uint8_t s[STATUS_BYTES];

    speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
    /* A complete invalid packet is dropped; the next packet still parses. */
    SEND(COMMAND_STOP, 0);
    SEND(COMMAND_STOP, 5);
    status(s);
    assert(s[S_REQUEST_ID] == 5 && s[S_RESULT] == RESULT_STOPPED);
    /* An unknown command loses framing: bytes are ignored until an IN. */
    SEND(0x99, COMMAND_PLAY, 6, 0, 0, 100, 100, FX_PRESET, PRESET_COIN,
         SPEAKER_IO_LATCH_STATUS);
    (void)speaker_io_input();
    status(s);
    assert(s[S_REQUEST_ID] == 5 && !(s[S_FLAGS] & STATUS_PLAYING));
    SEND(COMMAND_PLAY, 6, 0, 0, 100, 100, FX_PRESET, PRESET_COIN);
    status(s);
    assert(s[S_ACTIVE_ID] == 6 && s[S_PRESET] == PRESET_COIN);
    /* An invalid sequence length also loses framing. */
    SEND(COMMAND_PLAY, 7, 9, 0, 100, 100, FX_SEQUENCE, 9, BE16(440), BE16(10));
    (void)speaker_io_input();
    status(s);
    assert(s[S_ACTIVE_ID] == 6);
    /* An IN abandons a partial packet; a zero payload byte is data, not a latch. */
    SEND(COMMAND_PLAY, 8, 9, 0, 100, 100, FX_TONE, BE16(500));
    (void)speaker_io_input();
    SEND(COMMAND_STOP, 9);
    status(s);
    assert(s[S_REQUEST_ID] == 9 && !(s[S_FLAGS] & STATUS_PLAYING));
    SEND(COMMAND_PLAY, 10, 0, 0, 100, 100, FX_TONE, BE16(0x0100), BE16(0x0005));
    status(s);
    assert(s[S_ACTIVE_ID] == 10 && read_be16(s + S_FREQUENCY_HI) == 256);
    speaker_io_close();
}

static void test_queue_and_presets(void)
{
    uint8_t s[STATUS_BYTES];

    speaker_io_init(SPEAKER_IO_CLOCK_MANUAL);
    SEND(COMMAND_PLAY, 1, 0, 0, 100, 100, FX_TONE, BE16(800), BE16(20));
    SEND(COMMAND_PLAY, 2, 0, PLAY_ENQUEUE, 100, 100, FX_TONE, BE16(1600), BE16(20));
    status(s);
    assert(s[S_QUEUED] == 1 && s[S_RESULT] == RESULT_QUEUED);
    speaker_io_render(samples, 21 * FRAMES_PER_MS);
    status(s);
    assert(s[S_ACTIVE_ID] == 2 && read_be16(s + S_FREQUENCY_HI) == 1600);
    for (uint8_t preset = 1; preset < PRESET_COUNT; ++preset) {
        SEND(COMMAND_PLAY, preset, 255, 0, 100, 100, FX_PRESET, preset);
        assert(!silent(10) || preset == PRESET_FOOTSTEPS || preset == PRESET_HEARTBEAT);
    }
    SEND(COMMAND_STOP, 1);
    assert(silent(1));
    speaker_io_close();
}

int main(void)
{
    test_status_and_tone();
    test_pitch_accuracy();
    test_sweep_follows_engine();
    test_framing();
    test_queue_and_presets();
    return 0;
}
