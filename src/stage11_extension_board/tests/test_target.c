#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main attiny_firmware_main
#include "../flash_ATTiny85/target/main.c"
#undef main

static void start_write(void) {
    PINB = SDA_MASK; /* SCL low: START handshake may complete. */
    USI_START_vect();
    assert(start_pending);
    service_start();
    assert(!start_pending && receive_state == ADDRESS);
    USIDR = ATTINY85_I2C_ADDRESS << 1;
    USI_OVF_vect();
    assert(receive_state == ADDRESS_ACK && (DDRB & SDA_MASK));
    USI_OVF_vect();
    assert(receive_state == DATA && !(DDRB & SDA_MASK));
}

static void send_byte(uint8_t byte, bool final) {
    USIDR = byte;
    USI_OVF_vect();
    assert(receive_state == (final ? FINAL_ACK : DATA_ACK));
    assert(DDRB & SDA_MASK);
    USI_OVF_vect();
}

#define BE16(value) (uint8_t)((value) >> 8), (uint8_t)(value)
#define RUN(...) execute(&(command_t){{__VA_ARGS__}})

static void advance(uint16_t ms) {
    milliseconds += ms;
    service_effects();
}

static void test_timer_updates(void) {
    RUN(COMMAND_TONE, BE16(1000), BE16(1000));
    TCNT1 = 50;
    GTCCR = 0;
    set_frequency(1020);
    assert(TCNT1 == 50 && GTCCR == 0);
    assert(OCR1C == tone_config(1020).top);
    TCNT1 = 250;
    set_frequency(1030);
    assert(TCNT1 == 0 && GTCCR == 0); /* Do not miss a lowered TOP. */
    TCNT1 = 50;
    set_frequency(1030);
    assert(TCNT1 == 50); /* Identical pitches do not restart the waveform. */
    set_frequency(1);
    assert(TCNT1 == 0 && (TCCR1 & _BV(CTC1)));
    set_frequency(0);
    assert(!TCCR1 && !sound.frequency);
    RUN(COMMAND_STOP, CHANNEL_ALL);
}

static void test_effects(void) {
    RUN(COMMAND_STOP, CHANNEL_ALL);
    RUN(COMMAND_SWEEP, BE16(1000), BE16(4000), BE16(1000));
    assert(sound.frequency == 1000);
    advance(500);
    assert(sound.frequency == 2500);
    advance(499);
    assert(sound.frequency == 3997);
    advance(1);
    assert(sound.mode == EFFECT_NONE && !TCCR1);
    RUN(COMMAND_SWEEP, BE16(4000), BE16(1000), BE16(1000));
    advance(500);
    assert(sound.frequency == 2500);

    RUN(COMMAND_SIREN, BE16(1000), BE16(4000), BE16(100), BE16(200), BE16(650));
    advance(50);
    assert(sound.frequency == 2500);
    advance(50);
    assert(sound.frequency == 4000);
    advance(100);
    assert(sound.frequency == 2500);
    advance(100);
    assert(sound.frequency == 1000);
    advance(349);
    assert(sound.frequency == 2470);
    advance(1);
    assert(!TCCR1);
    RUN(COMMAND_SIREN, BE16(1000), BE16(4000), BE16(65535), BE16(65535), 0, 0);
    advance(65535);
    assert(sound.frequency == 4000 && sound.phase == 65535);
    advance(65535);
    assert(sound.frequency == 1000 && sound.phase == 0);

    RUN(COMMAND_SEQUENCE, 3, BE16(2),
        BE16(1000), BE16(100), BE16(0), BE16(50), BE16(2000), BE16(150));
    assert(sound.frequency == 1000);
    advance(100);
    assert(sound.mode == EFFECT_SEQUENCE && !TCCR1);
    advance(50);
    assert(sound.frequency == 2000);
    advance(150);
    assert(sound.frequency == 1000 && sound.repeats == 1);
    advance(300);
    assert(sound.mode == EFFECT_NONE && !TCCR1);
    RUN(COMMAND_SEQUENCE, 1, 0, 0, BE16(1234), BE16(1));
    advance(65535);
    assert(sound.frequency == 1234 && sound.mode == EFFECT_SEQUENCE);
    RUN(COMMAND_SEQUENCE, 1, BE16(65535), BE16(1), BE16(1));
    advance(65534);
    assert(sound.repeats == 1 && sound.frequency == 1);
    advance(1);
    assert(!TCCR1);

    RUN(COMMAND_FADE, CHANNEL_LEDS, 0, 255, BE16(1000));
    assert(red == 0 && blue == 0);
    advance(500);
    assert(red == 127 && blue == 127);
    RUN(COMMAND_BLUE, 42);
    advance(500);
    assert(red == 255 && blue == 42 && lights[0].mode == EFFECT_NONE);
    advance(1000);
    assert(red == 255 && blue == 42);
    RUN(COMMAND_FADE, CHANNEL_RED, 255, 0, BE16(1));
    advance(1);
    assert(red == 0 && blue == 42);

    RUN(COMMAND_BLINK, CHANNEL_RED, 200, BE16(100), BE16(200), BE16(2));
    assert(red == 200 && blue == 42);
    advance(100);
    assert(red == 0);
    advance(200);
    assert(red == 200);
    advance(300);
    assert(red == 0 && lights[0].mode == EFFECT_NONE);
    RUN(COMMAND_BLINK, CHANNEL_BLUE, 255, BE16(1), BE16(1), 0, 0);
    advance(65535);
    assert(blue == 0 && lights[1].mode == EFFECT_BLINK);
    advance(1);
    assert(blue == 255);

    RUN(COMMAND_BREATHE, CHANNEL_LEDS, 10, 210, BE16(100), BE16(300), BE16(850), 33);
    assert(red == 10 && blue == 10);
    advance(50);
    assert(red == 110 && blue == 110);
    advance(50);
    assert(red == 210);
    advance(150);
    assert(red == 110);
    advance(150);
    assert(red == 10);
    advance(450);
    assert(red == 33 && blue == 33 && lights[0].mode == EFFECT_NONE);
    RUN(COMMAND_BREATHE, CHANNEL_RED, 0, 255, BE16(65535), BE16(65535), 0, 0, 0);
    advance(65535);
    assert(red == 255 && lights[0].mode == EFFECT_BREATHE);
    advance(65535);
    assert(red == 0);

    RUN(COMMAND_ALTERNATE, 255, BE16(100), BE16(200), BE16(2));
    assert(red == 255 && blue == 0);
    advance(100);
    assert(red == 0 && blue == 255);
    advance(200);
    assert(red == 255 && blue == 0);
    advance(300);
    assert(red == 0 && blue == 0);
    RUN(COMMAND_ALTERNATE, 200, BE16(1), BE16(1), 0, 0);
    advance(65535);
    assert(red == 0 && blue == 200);

    RUN(COMMAND_CUE, BE16(1000), BE16(4000), BE16(1000), 0, 200, 200, 0);
    assert(sound.frequency == 1000 && red == 0 && blue == 200);
    advance(500);
    assert(sound.frequency == 2500 && red == 100 && blue == 100);
    advance(500);
    assert(!TCCR1 && red == 200 && blue == 0);

    RUN(COMMAND_SIREN, BE16(1000), BE16(4000), BE16(100), BE16(200), 0, 0);
    RUN(COMMAND_BREATHE, CHANNEL_LEDS, 0, 200, BE16(100), BE16(100), 0, 0, 0);
    RUN(COMMAND_STOP, CHANNEL_RED);
    advance(50);
    assert(red == 0 && blue == 100 && sound.frequency == 2500);
    RUN(COMMAND_STOP, CHANNEL_SOUND);
    advance(50);
    assert(!TCCR1 && red == 0 && blue == 200);
    RUN(COMMAND_STOP, CHANNEL_LEDS);
    advance(1000);
    assert(red == 0 && blue == 0 && !TCCR1);
    RUN(COMMAND_TONE, BE16(2000), BE16(100));
    RUN(COMMAND_BLINK, CHANNEL_LEDS, 255, BE16(1), BE16(1), 0, 0);
    RUN(COMMAND_STOP, CHANNEL_ALL);
    advance(1);
    assert(!TCCR1 && red == 0 && blue == 0);

    /* A 16-bit wall-clock wrap must not change effect durations. */
    milliseconds = serviced_ms = 65530;
    RUN(COMMAND_SWEEP, BE16(1), BE16(65535), BE16(65535));
    advance(32767);
    assert(sound.frequency == 32767);
    advance(32767);
    assert(sound.frequency == 65534);
    advance(1);
    assert(!TCCR1);
}

static void test_new_primitives(void) {
    RUN(COMMAND_SEGMENTS, 2, BE16(2), BE16(1000),
        BE16(3000), BE16(100), BE16(1000), BE16(200));
    assert(sound.frequency == 1000);
    advance(50);
    assert(sound.frequency == 2000);
    advance(50);
    assert(sound.frequency == 3000);
    advance(100);
    assert(sound.frequency == 2000);
    advance(100);
    assert(sound.frequency == 1000 && sound.repeats == 1);
    advance(300);
    assert(!TCCR1 && sound.mode == EFFECT_NONE);
    RUN(COMMAND_SEGMENTS, 1, 0, 0, BE16(65535), BE16(1), BE16(65535));
    advance(32767);
    assert(sound.frequency == 32769);
    advance(32768);
    assert(sound.frequency == 65535 && sound.mode == EFFECT_SEGMENTS);
    RUN(COMMAND_STOP, CHANNEL_SOUND);
    assert(!TCCR1);

    RUN(COMMAND_PULSES, 4, BE16(3000), BE16(10), BE16(30), BE16(10));
    assert(sound.frequency == 3000);
    advance(10);
    assert(!TCCR1);
    advance(30);
    assert(sound.frequency == 3000 && sound.index == 1);
    advance(10);
    assert(!TCCR1);
    advance(20);
    assert(sound.frequency == 3000 && sound.index == 2);
    advance(20);
    assert(sound.frequency == 3000 && sound.index == 3);
    advance(10);
    assert(!TCCR1 && sound.mode == EFFECT_NONE);
    RUN(COMMAND_PULSES, 1, BE16(1000), BE16(1), BE16(65535), BE16(65535));
    advance(1);
    assert(!TCCR1);
    RUN(COMMAND_PULSES, 2, BE16(1000), BE16(1), BE16(5), 0, 0);
    advance(6);
    assert(sound.frequency == 1000);
    advance(1);
    assert(!TCCR1);
    RUN(COMMAND_PULSES, 64, BE16(1000), BE16(1), 0, 0, 0, 0);
    advance(64);
    assert(!TCCR1 && sound.index == 64);
    RUN(COMMAND_PULSES, 3, BE16(1000), BE16(10), BE16(10), BE16(30));
    advance(59);
    assert(!TCCR1 && sound.index == 1); /* Final, longer gap. */
    advance(1);
    assert(sound.frequency == 1000 && sound.index == 2);
    advance(10);
    assert(!TCCR1 && sound.mode == EFFECT_NONE);
    RUN(COMMAND_PULSES, 64, BE16(1), BE16(65535), BE16(65535), BE16(65535));
    advance(65535);
    assert(!TCCR1 && sound.index == 0);
    advance(65535);
    assert(sound.frequency == 1 && sound.index == 1);

    RUN(COMMAND_VIBRATO, BE16(2000), BE16(2000), BE16(500), BE16(200), BE16(100));
    assert(sound.frequency == 1800);
    advance(25);
    assert(sound.frequency == 2000);
    advance(25);
    assert(sound.frequency == 2200);
    advance(25);
    assert(sound.frequency == 2000);
    advance(25);
    assert(sound.frequency == 1800);
    advance(400);
    assert(!TCCR1);
    RUN(COMMAND_VIBRATO, BE16(1000), BE16(3000), BE16(1000), BE16(100), BE16(100));
    advance(500);
    assert(sound.frequency == 1900);
    RUN(COMMAND_FADE, CHANNEL_RED, 0, 200, BE16(500));
    RUN(COMMAND_STOP, CHANNEL_SOUND);
    advance(250);
    assert(!TCCR1 && red == 100);
    RUN(COMMAND_VIBRATO, BE16(32768), BE16(32768), BE16(65535),
        BE16(32767), BE16(2));
    assert(sound.frequency == 1);
    advance(1);
    assert(sound.frequency == 65535);
    advance(65534);
    assert(!TCCR1);
    RUN(COMMAND_VIBRATO, BE16(2000), BE16(1000), BE16(1000), 0, 0, BE16(3));
    advance(500);
    assert(sound.frequency == 1500);
    RUN(COMMAND_TONE, BE16(440), BE16(10));
    advance(10);
    assert(!TCCR1);

    /* Check every pulse edge against an independently constructed schedule. */
    for (unsigned slowing = 0; slowing < 2; ++slowing) {
        command_t pulses = {{COMMAND_PULSES, 12, BE16(3000), BE16(40),
                             BE16(240), BE16(20)}};
        if (slowing) {
            pulses.bytes[7] = 20;
            pulses.bytes[9] = 240;
        }
        RUN(COMMAND_STOP, CHANNEL_ALL);
        execute(&pulses);
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
        assert(total == 1910 && !TCCR1 && sound.mode == EFFECT_NONE);
    }

    RUN(COMMAND_VIBRATO, BE16(2000), BE16(2000), BE16(1000),
        BE16(100), BE16(5));
    const uint16_t odd_cycle[] = {1900, 2000, 2100, 2034, 1968};
    for (unsigned ms = 0; ms < 1000; ++ms) {
        assert(sound.frequency == odd_cycle[ms % 5]);
        advance(1);
    }
    assert(!TCCR1);
}

static void test_packets(void) {
    const command_t valid[] = {
        {{COMMAND_SWEEP, BE16(1), BE16(65535), BE16(1)}},
        {{COMMAND_SIREN, BE16(1), BE16(2), BE16(1), BE16(1), 0, 0}},
        {{COMMAND_SEQUENCE, 8, BE16(1),
          BE16(1), BE16(65535), BE16(2), BE16(65535),
          BE16(3), BE16(65535), BE16(4), BE16(65535),
          BE16(5), BE16(65535), BE16(6), BE16(65535),
          BE16(7), BE16(65535), BE16(8), BE16(65535)}},
        {{COMMAND_FADE, CHANNEL_LEDS, 0, 255, BE16(1)}},
        {{COMMAND_BLINK, CHANNEL_RED, 255, BE16(1), BE16(1), BE16(1)}},
        {{COMMAND_BREATHE, CHANNEL_BLUE, 0, 255, BE16(1), BE16(1), 0, 0, 42}},
        {{COMMAND_ALTERNATE, 255, BE16(1), BE16(1), 0, 0}},
        {{COMMAND_STOP, CHANNEL_ALL}},
        {{COMMAND_CUE, BE16(1), BE16(2), BE16(1), 0, 255, 255, 0}},
        {{COMMAND_SEGMENTS, 7, BE16(1), BE16(1),
          BE16(2), BE16(65535), BE16(3), BE16(65535),
          BE16(4), BE16(65535), BE16(5), BE16(65535),
          BE16(6), BE16(65535), BE16(7), BE16(65535), BE16(8), BE16(65535)}},
        {{COMMAND_PULSES, 64, BE16(1), BE16(65535), 0, 0, BE16(65535)}},
        {{COMMAND_VIBRATO, BE16(32768), BE16(32768), BE16(65535),
          BE16(32767), BE16(65535)}}
    };
    for (unsigned n = 0; n < sizeof(valid) / sizeof(valid[0]); ++n) {
        const uint8_t *b = valid[n].bytes;
        uint8_t size = packet_length(b);
        assert(command_valid(b));
        /* Every truncation must discard, not execute, the partial effect. */
        for (uint8_t cut = 1; cut < size; ++cut) {
            start_write();
            for (uint8_t i = 0; i < cut; ++i) {
                send_byte(b[i], false);
            }
            assert(!dequeue(&(command_t){0}));
        }
        start_write();
        for (uint8_t i = 0; i < size; ++i) {
            send_byte(b[i], i == size - 1);
        }
        command_t received;
        assert(dequeue(&received) && memcmp(received.bytes, b, size) == 0);
        execute(&received);
        if (b[0] == COMMAND_SEQUENCE) {
            assert(sound.period == 524280UL);
            for (uint8_t i = 0; i < 8; ++i) {
                assert(sound.frequency == i + 1);
                advance(65535);
            }
            assert(!TCCR1);
        }
        if (b[0] == COMMAND_SEGMENTS) {
            assert(sound.period == 458745UL);
            for (uint8_t i = 0; i < 7; ++i) {
                assert(sound.frequency == i + 1);
                advance(65535);
            }
            assert(!TCCR1);
        }
        assert(!dequeue(&received));
    }

    const command_t invalid[] = {
        {{COMMAND_SWEEP, 0, 0, BE16(2), BE16(1)}},
        {{COMMAND_SWEEP, BE16(1), BE16(2), 0, 0}},
        {{COMMAND_SIREN, BE16(1), BE16(2), 0, 0, BE16(1), 0, 0}},
        {{COMMAND_SIREN, BE16(1), BE16(2), BE16(1), 0, 0, 0, 0}},
        {{COMMAND_SEQUENCE, 0}},
        {{COMMAND_SEQUENCE, 9}},
        {{COMMAND_SEQUENCE, 1, 0, 0, BE16(1), 0, 0}},
        {{COMMAND_FADE, 0, 0, 255, BE16(1)}},
        {{COMMAND_FADE, CHANNEL_SOUND, 0, 255, BE16(1)}},
        {{COMMAND_FADE, CHANNEL_RED, 0, 255, 0, 0}},
        {{COMMAND_BLINK, CHANNEL_RED, 255, 0, 0, BE16(1), 0, 0}},
        {{COMMAND_BLINK, CHANNEL_RED, 255, BE16(1), 0, 0, 0, 0}},
        {{COMMAND_BREATHE, CHANNEL_LEDS, 255, 0, BE16(1), BE16(1), 0, 0, 0}},
        {{COMMAND_BREATHE, CHANNEL_LEDS, 0, 255, 0, 0, BE16(1), 0, 0, 0}},
        {{COMMAND_ALTERNATE, 255, 0, 0, BE16(1), 0, 0}},
        {{COMMAND_STOP, 0}}, {{COMMAND_STOP, 8}},
        {{COMMAND_CUE, BE16(1), BE16(2), 0, 0, 0, 255, 255, 0}},
        {{COMMAND_SEGMENTS, 0}}, {{COMMAND_SEGMENTS, 8}},
        {{COMMAND_SEGMENTS, 1, BE16(1), 0, 0, BE16(1), BE16(1)}},
        {{COMMAND_SEGMENTS, 1, BE16(1), BE16(1), 0, 0, BE16(1)}},
        {{COMMAND_SEGMENTS, 1, BE16(1), BE16(1), BE16(2), 0, 0}},
        {{COMMAND_PULSES, 0}}, {{COMMAND_PULSES, 65}},
        {{COMMAND_PULSES, 1, 0, 0, BE16(1), 0, 0, 0, 0}},
        {{COMMAND_PULSES, 1, BE16(1), 0, 0, 0, 0, 0, 0}},
        {{COMMAND_VIBRATO, BE16(100), BE16(1000), BE16(100), BE16(100), BE16(10)}},
        {{COMMAND_VIBRATO, BE16(65000), BE16(1000), BE16(100), BE16(600), BE16(10)}},
        {{COMMAND_VIBRATO, BE16(1000), BE16(1000), 0, 0, BE16(10), BE16(10)}},
        {{COMMAND_VIBRATO, BE16(1000), BE16(1000), BE16(100), BE16(10), BE16(1)}}
    };
    RUN(COMMAND_TONE, BE16(1000), BE16(1000));
    for (unsigned n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n) {
        const uint8_t *b = invalid[n].bytes;
        assert(!command_valid(b));
        uint8_t size = command_length(b[0]);
        if (b[0] == COMMAND_SEQUENCE) {
            size = b[1] == 1 ? 8 : 2;
        } else if (b[0] == COMMAND_SEGMENTS) {
            size = b[1] == 1 ? 10 : 2;
        }
        start_write();
        for (uint8_t i = 0; i < size - 1; ++i) {
            send_byte(b[i], false);
        }
        USIDR = b[size - 1];
        USI_OVF_vect();
        assert(!(DDRB & SDA_MASK) && !(USICR & _BV(USIOIE)));
        assert(!dequeue(&(command_t){0}));
        assert(sound.frequency == 1000);
    }
}

int main(void) {
    assert(command_length(1) == 5 && command_length(2) == 2);
    assert(command_length(3) == 2 && command_length(0xff) == 0);
    uint8_t endian[] = {0x12, 0x34};
    assert(read_be16(endian) == 0x1234);

    for (unsigned value = 0; value < 256; ++value) {
        unsigned on = 0;
        for (unsigned phase = 0; phase < 256; ++phase) {
            on += led_on((uint8_t)value, (uint8_t)phase);
        }
        assert(on == (value == 255 ? 256 : value));
    }
    assert(tone_config(0).clock_select == 0);
    for (uint32_t frequency = 1; frequency <= 65535; ++frequency) {
        tone_config_t config = tone_config((uint16_t)frequency);
        assert(config.clock_select >= 1 && config.clock_select <= 15);
        uint32_t divisor = 1UL << (config.clock_select - 1);
        double actual = 8000000.0 / (divisor * (config.top + 1u) *
                                    (config.toggle ? 2u : 1u));
        double error = actual / frequency - 1;
        assert(error > -0.01 && error < 0.01);
    }

    start_write();
    send_byte(COMMAND_TONE, false);
    send_byte(0x03, false);
    send_byte(0xe8, false);
    send_byte(0x01, false);
    send_byte(0xf4, true); /* 1000 Hz, 500 ms */
    command_t command;
    assert(dequeue(&command) && !dequeue(&(command_t){0}));
    execute(&command);
    assert(sound.remaining == 500 && (TCCR1 & _BV(PWM1A)));
    for (unsigned i = 0; i < 15624; ++i) {
        TIM0_COMPA_vect();
        service_effects();
    }
    assert(sound.remaining == 1 && TCCR1);
    TIM0_COMPA_vect();
    service_effects();
    assert(sound.remaining == 0 && TCCR1 == 0 && !(PORTB & _BV(PB1)));

    execute(&(command_t){{COMMAND_TONE, 0, 1, 0xff, 0xff}});
    assert(sound.remaining == 65535 && (TCCR1 & _BV(CTC1)));
    execute(&(command_t){{COMMAND_TONE, 0, 0, 0, 0}});
    assert(TCCR1 == 0);
    execute(&(command_t){{COMMAND_TONE, 1, 0, 0, 0}});
    assert(TCCR1 == 0);
    execute(&(command_t){{COMMAND_TONE, 1, 0, 0, 10}});
    execute(&(command_t){{COMMAND_TONE, 2, 0, 0, 20}});
    assert(sound.remaining == 20); /* New tone replaces the current tone. */

    start_write();
    send_byte(COMMAND_RED, false); /* Truncated packet, then repeated START. */
    assert(!dequeue(&command));
    start_write();
    send_byte(COMMAND_BLUE, false);
    send_byte(255, true);
    assert(dequeue(&command));
    execute(&command);
    assert(blue == 255);

    start_write();
    USIDR = 0x99;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK) && !(USICR & _BV(USIOIE)));
    assert(!dequeue(&command));

    for (unsigned i = 0; i < QUEUE_SIZE - 1; ++i) {
        start_write();
        send_byte(COMMAND_RED, false);
        send_byte((uint8_t)i, true);
    }
    start_write();
    send_byte(COMMAND_RED, false);
    USIDR = 20;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK)); /* Full queue NACKs the final payload byte. */
    for (unsigned i = 0; i < QUEUE_SIZE - 1; ++i) {
        assert(dequeue(&command) && command.bytes[1] == i);
    }
    assert(!dequeue(&command));

    PINB = SCL_MASK; /* Master stalls after START: no ISR busy wait. */
    USI_START_vect();
    service_start();
    assert(start_pending);
    RUN(COMMAND_CUE, BE16(1000), BE16(2000), BE16(2), 0, 255, 255, 0);
    for (unsigned i = 0; i < 63; ++i) {
        TIM0_COMPA_vect();
        service_start();
        service_effects();
    }
    assert(start_pending && !TCCR1 && red == 255 && blue == 0);
    PINB = SCL_MASK | SDA_MASK; /* Cancel with STOP. */
    service_start();
    assert(!start_pending);

    start_write();
    receive_state = ADDRESS;
    USIDR = (ATTINY85_I2C_ADDRESS << 1) | 1u;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK)); /* Read requests are not supported. */
    test_timer_updates();
    test_effects();
    test_new_primitives();
    test_packets();
    puts("ATtiny85 protocol, USI states, PWM and autonomous effects tests passed.");
    return 0;
}
