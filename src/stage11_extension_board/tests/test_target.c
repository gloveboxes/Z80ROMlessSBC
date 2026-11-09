#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main attiny_firmware_main
#include "../flash_ATTiny85/target/main.c"
#undef main

#define BE16(value) (uint8_t)((value) >> 8), (uint8_t)(value)
#define PLAY(id, priority, flags, ...) \
    execute((uint8_t[]){COMMAND_PLAY, id, priority, flags, 100, 100, __VA_ARGS__})
#define CONTROL(...) execute((uint8_t[]){__VA_ARGS__})

static void reset(void) {
    memset(&sound, 0, sizeof(sound));
    playback_count = event_count = event_head = event_overflow = 0;
    event_sequence = completed_count = rejected_count = 0;
    command_pending = start_pending = packet_size = expected_size = 0;
    milliseconds = serviced_ms = 0;
    request_id = request_result = 0;
    tone_stop();
    publish_status();
}

static void advance(uint16_t ms) {
    milliseconds += ms;
    service_effects();
    publish_status();
}

static void drain(void) {
    while (event_count) {
        uint16_t sequence = events[event_head].sequence;
        CONTROL(COMMAND_ACK_EVENT, BE16(sequence));
    }
}

static void start(bool read) {
    PINB = SDA_MASK;
    USI_START_vect();
    assert(start_pending);
    service_start();
    assert(!start_pending && receive_state == ADDRESS);
    USIDR = (ATTINY85_I2C_ADDRESS << 1) | read;
    USI_OVF_vect();
    assert(receive_state == ADDRESS_ACK && (DDRB & SDA_MASK));
    USI_OVF_vect();
    assert(receive_state == (read ? TRANSMIT : DATA));
}

static void send_byte(uint8_t value, bool final) {
    USIDR = value;
    USI_OVF_vect();
    assert(receive_state == (final ? FINAL_ACK : DATA_ACK));
    USI_OVF_vect();
}

static uint8_t read_byte(bool last) {
    uint8_t byte = USIDR;
    assert(receive_state == TRANSMIT && (DDRB & SDA_MASK));
    USI_OVF_vect();
    assert(receive_state == TRANSMIT_ACK && !(DDRB & SDA_MASK));
    USIDR = last ? 1 : 0;
    USI_OVF_vect();
    if (last)
        assert(!(DDRB & SDA_MASK) && !(USICR & _BV(USIOIE)));
    else
        assert(receive_state == TRANSMIT);
    return byte;
}

static void test_timers(void) {
    reset();
    assert(tone_config(0).clock_select == 0);
    for (uint32_t hz = 1; hz <= 65535; ++hz) {
        tone_config_t config = tone_config(hz);
        assert(config.clock_select && config.clock_select <= 15);
        double actual = 8000000.0 / ((1UL << (config.clock_select - 1)) *
            (config.top + 1u) * (config.toggle ? 2u : 1u));
        assert(actual / hz > .99 && actual / hz < 1.01);
    }
    PLAY(1, 0, 0, FX_TONE, BE16(1000), BE16(10));
    TCNT1 = 50;
    GTCCR = 0;
    set_frequency(1020);
    assert(TCNT1 == 50 && !GTCCR);
    TCNT1 = 250;
    set_frequency(1030);
    assert(TCNT1 == 0);
    set_frequency(1);
    assert(TCCR1 & _BV(CTC1));
    for (unsigned i = 0; i < 9; ++i) {
        TIM0_COMPA_vect();
        service_effects();
    }
    assert(TCCR1);
    TIM0_COMPA_vect();
    service_effects();
    assert(!TCCR1 && completed_count == 1);
    milliseconds = serviced_ms = 65530;
    PLAY(2, 0, 0, FX_TONE, BE16(2000), BE16(10));
    advance(10);
    assert(!TCCR1 && completed_count == 2);
}

static void test_shapes(void) {
    reset();
    PLAY(1, 1, 0, FX_SWEEP, BE16(4000), BE16(1000), BE16(1000));
    advance(500);
    assert(sound.frequency == 2500);
    advance(500);
    assert(!sound.mode && !TCCR1);
    drain();
    PLAY(2, 1, 0, FX_SIREN, BE16(1000), BE16(4000), BE16(100), BE16(200), BE16(650));
    advance(100);
    assert(sound.frequency == 4000);
    advance(100);
    assert(sound.frequency == 2500);
    advance(100);
    assert(sound.frequency == 1000);
    advance(350);
    assert(!sound.mode);
    drain();
    PLAY(3, 1, 0, FX_SEQUENCE, 3, BE16(2),
         BE16(1000), BE16(100), BE16(0), BE16(50), BE16(2000), BE16(150));
    advance(100);
    assert(sound.mode && !TCCR1); /* Rest is playing, not completion. */
    advance(50);
    assert(sound.frequency == 2000);
    advance(150);
    assert(sound.frequency == 1000 && sound.repeats == 1);
    advance(300);
    assert(!sound.mode);
    drain();
    PLAY(4, 1, 0, FX_SEGMENTS, 2, BE16(2), BE16(1000),
         BE16(3000), BE16(100), BE16(1000), BE16(200));
    advance(50);
    assert(sound.frequency == 2000);
    advance(50);
    assert(sound.frequency == 3000);
    advance(200);
    assert(sound.frequency == 1000 && sound.repeats == 1);
    advance(300);
    assert(!sound.mode);
    drain();
    PLAY(5, 1, 0, FX_SEGMENTS, 1, BE16(0), BE16(65535), BE16(1), BE16(65535));
    advance(32767);
    assert(sound.frequency == 32769);
    advance(32768);
    assert(sound.frequency == 65535 && sound.mode);
    drain();
    PLAY(6, 1, 0, FX_GATE, BE16(1000), BE16(100), BE16(200), BE16(650));
    advance(100);
    assert(!TCCR1 && sound.mode);
    advance(200);
    assert(sound.frequency == 1000);
    advance(350);
    assert(!sound.mode);
    drain();
    for (uint8_t pattern = 0; pattern < 2; ++pattern) {
        PLAY(7, 1, 0, FX_ARPEGGIO, BE16(2560), BE16(100), BE16(450), pattern);
        assert(sound.frequency == 2560);
        advance(100);
        assert(sound.frequency == (pattern ? 3040 : 3230));
        advance(100);
        assert(sound.frequency == 3840);
        advance(100);
        assert(sound.frequency == 5120);
        advance(100);
        assert(sound.frequency == 2560);
        advance(50);
        assert(!sound.mode);
        drain();
    }
}

static void test_pulses_vibrato_updates(void) {
    reset();
    for (unsigned slowing = 0; slowing < 2; ++slowing) {
        uint8_t b[] = {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_PULSES, 12,
                       BE16(3000), BE16(40), BE16(240), BE16(20)};
        if (slowing) {
            b[13] = 20;
            b[15] = 240;
        }
        execute(b);
        unsigned total = 0;
        for (unsigned pulse = 0; pulse < 12; ++pulse) {
            for (unsigned ms = 0; ms < 40; ++ms) {
                assert(sound.frequency == 3000);
                advance(1);
                ++total;
            }
            if (pulse < 11) {
                unsigned gap = slowing ? 20 + 22 * pulse : 240 - 22 * pulse;
                for (unsigned ms = 0; ms < gap; ++ms) {
                    assert(!TCCR1);
                    advance(1);
                    ++total;
                }
            }
        }
        assert(total == 1910 && !sound.mode);
        drain();
    }
    PLAY(2, 1, 0, FX_PULSES, 1, BE16(1000), BE16(1), BE16(65535), BE16(65535));
    advance(1);
    assert(!sound.mode);
    drain();
    PLAY(3, 1, 0, FX_PULSES, 2, BE16(1000), BE16(1), BE16(5), 0, 0);
    advance(6);
    assert(sound.frequency == 1000);
    advance(1);
    assert(!sound.mode);
    drain();
    PLAY(4, 1, 0, FX_PULSES, 64, BE16(1000), BE16(1), 0, 0, 0, 0);
    advance(64);
    assert(!sound.mode && sound.index == 64);
    drain();
    PLAY(5, 1, 0, FX_VIBRATO, BE16(2000), BE16(2000), BE16(1000), BE16(100), BE16(5));
    const uint16_t odd[] = {1900, 2000, 2100, 2034, 1968};
    for (unsigned ms = 0; ms < 1000; ++ms) {
        assert(sound.frequency == odd[ms % 5]);
        advance(1);
    }
    assert(!sound.mode);
    drain();
    PLAY(6, 1, 0, FX_VIBRATO, BE16(1000), BE16(3000), BE16(1000), BE16(100), BE16(100));
    advance(25);
    assert(sound.vibrato_phase == 25 && sound.phase == 25);
    CONTROL(COMMAND_UPDATE, 90, 6, UPDATE_PITCH | UPDATE_SPEED | UPDATE_MODULATION,
            150, 200, BE16(200), BE16(200));
    assert(request_result == RESULT_UPDATED && sound.vibrato_phase == 50 && sound.phase == 25);
    uint16_t remaining = sound.remaining;
    advance(25);
    assert(sound.phase == 75 && sound.remaining == remaining - 50);
    assert(sound.vibrato_phase == 100);
    CONTROL(COMMAND_UPDATE, 91, 6, UPDATE_MODULATION, 0, 0, BE16(1000), BE16(100));
    assert(request_result == RESULT_INVALID && sound.depth == 200);
    CONTROL(COMMAND_UPDATE, 92, 7, UPDATE_PITCH, 100, 0, 0, 0, 0, 0);
    assert(request_result == RESULT_NO_MATCH && sound.id == 6);
    drain();
    CONTROL(COMMAND_STOP, 93);
    PLAY(7, 1, 0, FX_SWEEP, BE16(1000), BE16(3000), BE16(1000));
    advance(500);
    CONTROL(COMMAND_UPDATE, 94, 7, UPDATE_SPEED, 0, 25, 0, 0, 0, 0);
    advance(1999);
    assert(sound.mode && sound.remaining == 1);
    advance(1);
    assert(!sound.mode);
    drain();
    PLAY(8, 1, 0, FX_TONE, BE16(65535), BE16(65535));
    CONTROL(COMMAND_UPDATE, 95, 8, UPDATE_SPEED | UPDATE_PITCH, 200, 200, 0, 0, 0, 0);
    assert(sound.frequency == 65535);
    advance(32767);
    assert(sound.remaining == 1);
    advance(1);
    assert(!sound.mode);
    drain();
    PLAY(9, 1, 0, FX_GATE, BE16(1000), BE16(1), BE16(1), 0, 0);
    CONTROL(COMMAND_UPDATE, 96, 9, UPDATE_SPEED, 0, 200, 0, 0, 0, 0);
    advance(65535); /* Catch-up must not truncate scaled time. */
    assert(sound.mode && sound.frequency == 1000);
}

static void test_scheduling(void) {
    reset();
    PLAY(1, 5, 0, FX_TONE, BE16(1000), BE16(100));
    PLAY(2, 4, 0, FX_TONE, BE16(2000), BE16(100));
    assert(request_result == RESULT_PRIORITY && sound.id == 1 && rejected_count == 1);
    drain();
    PLAY(3, 5, 0, FX_TONE, BE16(3000), BE16(100));
    assert(sound.id == 3 && events[event_head].type == EVENT_INTERRUPTED);
    drain();
    PLAY(4, 6, 0, FX_TONE, BE16(4000), BE16(100));
    assert(sound.id == 4);
    drain();
    PLAY(5, 0, PLAY_ENQUEUE, FX_TONE, BE16(5000), BE16(10));
    PLAY(6, 255, PLAY_ENQUEUE, FX_TONE, BE16(6000), BE16(10));
    assert(playback_count == 2 && sound.id == 4); /* FIFO ignores priority. */
    PLAY(7, 255, PLAY_ENQUEUE, FX_TONE, BE16(7000), BE16(10));
    assert(request_result == RESULT_QUEUE_FULL && playback_count == 2);
    drain();
    advance(100);
    assert(sound.id == 5 && playback_count == 1 && completed_count == 1);
    drain();
    advance(10);
    assert(sound.id == 6 && !playback_count);
    drain();
    advance(10);
    assert(!sound.mode && completed_count == 3);
    drain();
    PLAY(8, 1, 0, FX_GATE, BE16(1000), BE16(10), BE16(10), 0, 0);
    PLAY(9, 1, PLAY_ENQUEUE, FX_TONE, BE16(2000), BE16(10));
    drain();
    CONTROL(COMMAND_CLEAR_QUEUE, 10);
    assert(!playback_count && sound.id == 8 && request_result == RESULT_CLEARED);
    assert(events[event_head].type == EVENT_CANCELLED);
    drain();
    PLAY(11, 1, PLAY_ENQUEUE, FX_TONE, BE16(2000), BE16(10));
    drain();
    CONTROL(COMMAND_STOP, 12);
    assert(!sound.mode && !playback_count && !TCCR1);
    assert(events[event_head].type == EVENT_STOPPED);
    assert(events[(event_head + 1u) % EVENT_CAPACITY].type == EVENT_CANCELLED);
    advance(1000);
    assert(!TCCR1); /* Discarded sounds never resume. */
    drain();
    PLAY(13, 1, 0, FX_TONE, BE16(1000), BE16(10));
    PLAY(14, 1, PLAY_ENQUEUE, FX_SEQUENCE, 8, BE16(1),
         BE16(1000), BE16(1), BE16(2000), BE16(1),
         BE16(3000), BE16(1), BE16(4000), BE16(1),
         BE16(5000), BE16(1), BE16(6000), BE16(1),
         BE16(7000), BE16(1), BE16(8000), BE16(1));
    drain();
    advance(10);
    assert(sound.id == 14 && sound.count == 8);
    for (unsigned note = 0; note < 8; ++note) {
        assert(sound.frequency == (note + 1) * 1000);
        advance(1);
    }
    assert(!sound.mode);
    drain();
    for (uint8_t id = 1; id < PRESET_COUNT; ++id) {
        assert(effect_valid(presets[id - 1]));
        PLAY(id, 1, 0, FX_PRESET, id);
        assert(sound.preset == id && sound.mode == presets[id - 1][0]);
        drain();
        advance(5000);
        bool looping = id == PRESET_ENGINE || id == PRESET_HEARTBEAT ||
                       id == PRESET_LOW_HEALTH || id == PRESET_TIME_WARNING;
        assert((sound.mode != 0) == looping);
        if (!looping)
            assert(events[event_head].id == id && events[event_head].type == EVENT_COMPLETED);
        drain();
        CONTROL(COMMAND_STOP, 100);
        drain();
    }
}

static void test_status_bus(void) {
    reset();
    uint8_t b[] = {COMMAND_PLAY, 23, 3, 0, 100, 100, FX_TONE, BE16(2000), BE16(100)};
    for (unsigned cut = 1; cut < sizeof(b); ++cut) {
        start(false);
        for (unsigned i = 0; i < cut; ++i)
            send_byte(b[i], false);
        assert(!command_pending);
    }
    start(false);
    for (unsigned i = 0; i < sizeof(b); ++i)
        send_byte(b[i], i == sizeof(b) - 1);
    assert(command_pending && !sound.mode);
    USIDR = 0xaa; /* Extra bytes cannot overwrite or undo the accepted packet. */
    USI_OVF_vect();
    assert(command_pending);
    /* A pending command survives another START and a rejected write. */
    start(false);
    USIDR = COMMAND_STOP;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK) && command_pending);
    start(true);
    assert(read_byte(false) == PROTOCOL_VERSION);
    assert(read_byte(true) & STATUS_COMMAND_PENDING);
    service_command();
    assert(sound.id == 23 && !command_pending);
    start(true);
    uint8_t snapshot[STATUS_BYTES];
    for (unsigned i = 0; i < STATUS_BYTES; ++i) {
        snapshot[i] = read_byte(i == STATUS_BYTES - 1);
        if (i == 2) {
            advance(100); /* The in-flight read must retain one coherent snapshot. */
        }
    }
    assert(snapshot[S_FLAGS] & STATUS_PLAYING);
    assert(snapshot[S_ACTIVE_ID] == 23 && snapshot[S_PRIORITY] == 3);
    assert(read_be16(snapshot + S_FREQUENCY_HI) == 2000);
    assert(snapshot[S_EVENT_TYPE] == EVENT_STARTED);
    assert(event_count == 2); /* Reads never consume events. */
    start(true);
    for (unsigned i = 0; i < STATUS_BYTES + 2; ++i) {
        uint8_t value = read_byte(i == STATUS_BYTES + 1);
        if (i >= STATUS_BYTES)
            assert(value == 0xff);
    }
    uint16_t sequence = events[event_head].sequence;
    CONTROL(COMMAND_ACK_EVENT, BE16(sequence + 1));
    assert(event_count == 2);
    CONTROL(COMMAND_ACK_EVENT, BE16(sequence));
    assert(event_count == 1);
    CONTROL(COMMAND_ACK_EVENT, BE16(sequence));
    assert(event_count == 1); /* ACK retries are idempotent. */
    drain();
    for (unsigned i = 0; i < 8; ++i)
        event(i + 1, EVENT_REJECTED);
    publish_status();
    assert(event_count == EVENT_CAPACITY && (published[S_FLAGS] & STATUS_EVENT_OVERFLOW));
    assert(events[event_head].id == 1); /* Overflow never overwrites unread events. */
    drain();
    assert(!event_overflow);
    event_sequence = 65535;
    event(1, EVENT_STARTED);
    assert(events[event_head].sequence == 1);
    PINB = SCL_MASK;
    USI_START_vect();
    service_start();
    assert(start_pending);
    PLAY(44, 1, 0, FX_TONE, BE16(2000), BE16(2));
    advance(2);
    assert(!sound.mode && start_pending);
    PINB = SCL_MASK | SDA_MASK;
    service_start();
    assert(!start_pending);
    start(false);
    receive_state = ADDRESS;
    USIDR = 0x43 << 1;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK) && !(USICR & _BV(USIOIE)));
}

static void test_packet_bounds(void) {
    reset();
    const uint8_t effects[][36] = {
        {FX_SEQUENCE, 8, BE16(1),
         BE16(1), BE16(65535), BE16(2), BE16(65535),
         BE16(3), BE16(65535), BE16(4), BE16(65535),
         BE16(5), BE16(65535), BE16(6), BE16(65535),
         BE16(7), BE16(65535), BE16(8), BE16(65535)},
        {FX_SEGMENTS, 7, BE16(1), BE16(1),
         BE16(2), BE16(65535), BE16(3), BE16(65535), BE16(4), BE16(65535),
         BE16(5), BE16(65535), BE16(6), BE16(65535),
         BE16(7), BE16(65535), BE16(8), BE16(65535)}
    };
    for (unsigned n = 0; n < 2; ++n) {
        uint8_t b[COMMAND_MAX_BYTES] = {COMMAND_PLAY, 1, 1, 0, 100, 100};
        memcpy(b + 6, effects[n], effect_length(effects[n]));
        unsigned size = packet_length(b);
        assert(command_valid(b) && size <= COMMAND_MAX_BYTES);
        for (unsigned cut = 1; cut < size; ++cut) {
            start(false);
            for (unsigned i = 0; i < cut; ++i)
                send_byte(b[i], false);
            assert(!command_pending);
        }
        start(false);
        for (unsigned i = 0; i < size; ++i)
            send_byte(b[i], i == size - 1);
        service_command();
        assert(sound.period == (n ? 458745UL : 524280UL));
        for (unsigned i = 0; i < (n ? 7u : 8u); ++i)
            advance(65535);
        assert(!sound.mode);
        drain();
    }
    const uint8_t invalid[][COMMAND_MAX_BYTES] = {
        {0xff}, {COMMAND_PLAY, 0, 1, 0, 100, 100, FX_PRESET, 1},
        {COMMAND_PLAY, 1, 1, 2, 100, 100, FX_PRESET, 1},
        {COMMAND_PLAY, 1, 1, 0, 24, 100, FX_PRESET, 1},
        {COMMAND_PLAY, 1, 1, 0, 100, 201, FX_PRESET, 1},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_PRESET, PRESET_COUNT},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_SEQUENCE, 0},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_SEQUENCE, 9},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_SEGMENTS, 8},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_TONE, BE16(0), BE16(10)},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_GATE, BE16(1), BE16(1), 0, 0, 0, 0},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_PULSES, 65, BE16(1), BE16(1), 0, 0, 0, 0},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_ARPEGGIO, BE16(32768), BE16(1), 0, 0, 0},
        {COMMAND_PLAY, 1, 1, 0, 100, 100, FX_VIBRATO,
         BE16(100), BE16(200), BE16(100), BE16(100), BE16(2)},
        {COMMAND_UPDATE, 1, 1, 0}, {COMMAND_UPDATE, 1, 1, UPDATE_SPEED, 0, 0}
    };
    PLAY(99, 255, 0, FX_TONE, BE16(1234), BE16(65535));
    for (unsigned n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n) {
        assert(!command_valid(invalid[n]));
        start(false);
        bool rejected = false;
        for (unsigned i = 0; i < COMMAND_MAX_BYTES; ++i) {
            USIDR = invalid[n][i];
            USI_OVF_vect();
            if (!(DDRB & SDA_MASK)) {
                rejected = true;
                break;
            }
            USI_OVF_vect();
        }
        assert(rejected && !command_pending);
        assert(sound.id == 99 && sound.frequency == 1234);
    }
}

int main(void) {
    test_timers();
    test_shapes();
    test_pulses_vibrato_updates();
    test_scheduling();
    test_status_bus();
    test_packet_bounds();
    puts("Sound primitives, priority/FIFO, live updates, events and USI read/write tests passed.");
    return 0;
}
