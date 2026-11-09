#include <assert.h>
#include <setjmp.h>
#include <string.h>

#define main pico_test_main
#include "../test_ATTiny85/main.c"
#undef main

typedef struct {
    uint8_t bytes[COMMAND_MAX_BYTES];
    size_t size;
    unsigned at_ms;
} write_t;

static write_t writes[4096];
static size_t write_count, fail_at;
static unsigned elapsed_ms;
static bool short_write, fail_all_writes, running_main;
static jmp_buf main_exit;
static size_t stop_after_writes;
static unsigned stdio_initializations, i2c_initializations, gpio_configurations;
static unsigned usb_connection_checks;

void sleep_ms(unsigned ms) {
    elapsed_ms += ms;
    assert(!running_main || elapsed_ms < 180000);
}
void stdio_init_all(void) { ++stdio_initializations; }
bool stdio_usb_connected(void) {
    ++usb_connection_checks;
    return false;
}
void tight_loop_contents(void) { assert(!running_main); }
void gpio_set_function(unsigned pin, unsigned function) {
    assert((pin == PIN_SDA || pin == PIN_SCL) && function == GPIO_FUNC_I2C);
    ++gpio_configurations;
}
unsigned i2c_init(void *i2c, unsigned baud) {
    assert(i2c == i2c0 && baud == 100000);
    ++i2c_initializations;
    return baud;
}
int i2c_write_timeout_us(void *i2c, uint8_t address, const uint8_t *bytes,
                         size_t size, bool nostop, unsigned timeout_us) {
    assert(i2c == i2c0 && address == ATTINY85_I2C_ADDRESS);
    assert(!nostop && timeout_us == 50000);
    if (running_main) {
        assert(stdio_initializations == 1 && i2c_initializations == 1);
        assert(gpio_configurations == 2);
        if (write_count == stop_after_writes) {
            longjmp(main_exit, 1);
        }
    }
    assert(write_count < sizeof(writes) / sizeof(writes[0]));
    assert(size <= COMMAND_MAX_BYTES);
    assert(size == packet_length(bytes));
    assert(command_valid(bytes));
    write_t *write = &writes[write_count++];
    memcpy(write->bytes, bytes, size);
    write->size = size;
    write->at_ms = elapsed_ms;
    if (fail_all_writes || write_count - 1 == fail_at) {
        return short_write ? (int)size - 1 : -1;
    }
    return (int)size;
}

static void run_main_until(size_t limit) {
    write_count = elapsed_ms = 0;
    stdio_initializations = i2c_initializations = gpio_configurations = 0;
    usb_connection_checks = 0;
    stop_after_writes = limit;
    running_main = true;
    if (setjmp(main_exit) == 0) {
        pico_test_main();
        assert(false);
    }
    running_main = false;
    assert(write_count == limit);
    assert(usb_connection_checks == 0);
}

int main(void) {
    fail_at = SIZE_MAX;
    assert(test_led_channels());
    const uint8_t all_stop[] = {COMMAND_STOP, CHANNEL_ALL};
    assert(writes[0].size == sizeof(all_stop));
    assert(memcmp(writes[0].bytes, all_stop, sizeof(all_stop)) == 0);
    assert(write_count == 7 && elapsed_ms == 5400);
    assert(writes[5].bytes[0] == COMMAND_FADE && writes[5].at_ms == 3000);
    assert(writes[6].bytes[0] == COMMAND_FADE && writes[6].at_ms == 4200);

    write_count = elapsed_ms = 0;
    fail_at = SIZE_MAX;
    assert(test_arcade_effects());
    assert(sizeof(arcade_effects) / sizeof(arcade_effects[0]) == 36);
    assert(write_count == 37);
    const uint8_t expected_commands[] = {
        COMMAND_SEQUENCE, COMMAND_CUE, COMMAND_SEQUENCE, COMMAND_SEQUENCE,
        COMMAND_CUE, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE,
        COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SWEEP,
        COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_CUE,
        COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SWEEP,
        COMMAND_SIREN, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE,
        COMMAND_SWEEP, COMMAND_SEQUENCE, COMMAND_SEQUENCE, COMMAND_SEQUENCE,
        COMMAND_SEQUENCE, COMMAND_SEQUENCE,
        COMMAND_SEGMENTS, COMMAND_SEGMENTS, COMMAND_PULSES, COMMAND_PULSES,
        COMMAND_VIBRATO, COMMAND_VIBRATO
    };
    const unsigned expected_durations[] = {
        250, 220, 330, 540, 750, 720, 1140, 1120, 2250,
        480, 600, 240, 55, 35, 100, 650,
        770, 960, 365, 900, 800, 770, 505, 300, 300, 1600, 3000, 440, 720, 1120,
        1400, 900, 1910, 1250, 1500, 1200
    };
    unsigned next_ms = 0;
    for (unsigned i = 0; i < 36; ++i) {
        const write_t *w = &writes[i];
        assert(w->bytes[0] == expected_commands[i] && w->at_ms == next_ms);
        unsigned duration;
        if (w->bytes[0] == COMMAND_SEQUENCE || w->bytes[0] == COMMAND_SEGMENTS) {
            duration = 0;
            unsigned offset = w->bytes[0] == COMMAND_SEQUENCE ? 6 : 8;
            for (unsigned step = 0; step < w->bytes[1]; ++step) {
                duration += read_be16(w->bytes + offset + 4 * step);
            }
            assert(read_be16(w->bytes + 2) != 0);
            duration *= read_be16(w->bytes + 2);
        } else if (w->bytes[0] == COMMAND_PULSES) {
            unsigned count = w->bytes[1];
            unsigned start = read_be16(w->bytes + 6), end = read_be16(w->bytes + 8);
            duration = count * read_be16(w->bytes + 4);
            for (unsigned gap = 0; gap < count - 1; ++gap) {
                duration += start <= end ? start + (end - start) * gap / (count - 2) :
                                          start - (start - end) * gap / (count - 2);
            }
        } else if (w->bytes[0] == COMMAND_SIREN) {
            duration = read_be16(w->bytes + 9);
            assert(duration != 0);
        } else {
            duration = read_be16(w->bytes + 5);
        }
        assert(duration == expected_durations[i]);
        assert(arcade_effects[i].wait_ms == duration + 200);
        next_ms += duration + 200;
    }
    const uint16_t scale_hz[] = {1047, 1175, 1319, 1397, 1568, 1760, 1976, 2093};
    assert(writes[8].bytes[1] == 8 && read_be16(writes[8].bytes + 2) == 1);
    for (unsigned note = 0; note < 8; ++note) {
        assert(read_be16(writes[8].bytes + 4 + 4 * note) == scale_hz[note]);
        assert(read_be16(writes[8].bytes + 6 + 4 * note) == (note == 7 ? 500 : 250));
    }
    assert(read_be16(writes[9].bytes + 2) == 3); /* Repeated pellet chomps. */
    assert(read_be16(writes[10].bytes + 2) == 3);
    assert(read_be16(writes[11].bytes + 1) < read_be16(writes[11].bytes + 3));
    assert(read_be16(writes[15].bytes + 1) > read_be16(writes[15].bytes + 3));
    assert(writes[15].bytes[7] == 255 && writes[15].bytes[8] == 0);
    for (unsigned step = 0; step < 3; ++step) {
        /* Footstep and countdown rests shrink; combo pitches climb. */
        assert(read_be16(writes[16].bytes + 10 + 8 * step) >
               read_be16(writes[16].bytes + 18 + 8 * step));
        assert(read_be16(writes[21].bytes + 10 + 8 * step) >
               read_be16(writes[21].bytes + 18 + 8 * step));
    }
    for (unsigned note = 0; note < 7; ++note) {
        assert(read_be16(writes[27].bytes + 4 + 4 * note) <
               read_be16(writes[27].bytes + 8 + 4 * note));
    }
    assert(read_be16(writes[19].bytes + 1) < read_be16(writes[19].bytes + 3));
    assert(read_be16(writes[20].bytes + 5) == 30 &&
           read_be16(writes[20].bytes + 7) == 30);
    assert(read_be16(writes[22].bytes + 32) == 180); /* Low explosion tail. */
    assert(read_be16(writes[25].bytes + 8) == 0); /* Sonar rest. */
    assert(read_be16(writes[26].bytes + 8) == 0 &&
           read_be16(writes[26].bytes + 16) == 0); /* Heartbeat rests. */
    assert(writes[36].at_ms == next_ms && elapsed_ms == next_ms);
    assert(memcmp(writes[36].bytes, all_stop, sizeof(all_stop)) == 0);
    write_count = elapsed_ms = 0;
    fail_at = SIZE_MAX;
    assert(test_cycle());
    const size_t cycle_writes = write_count;
    const unsigned cycle_ms = elapsed_ms;
    assert(cycle_writes < 100); /* Effects must not stream per-frame updates. */
    unsigned commands_seen = 0;
    bool finite_sequence = false, infinite_sequence = false, rest = false;
    bool finite_siren = false, infinite_siren = false;
    bool finite_blink = false, infinite_blink = false;
    bool finite_breathe = false, infinite_breathe = false;
    bool finite_alternate = false, infinite_alternate = false;
    unsigned stop_masks = 0;
    for (size_t i = 0; i < cycle_writes; ++i) {
        const uint8_t *b = writes[i].bytes;
        commands_seen |= 1u << b[0];
        switch (b[0]) {
        case COMMAND_SEQUENCE:
            finite_sequence |= read_be16(b + 2) != 0;
            infinite_sequence |= read_be16(b + 2) == 0;
            for (unsigned step = 0; step < b[1]; ++step) {
                rest |= read_be16(b + 4 + 4 * step) == 0;
            }
            break;
        case COMMAND_SIREN:
            finite_siren |= read_be16(b + 9) != 0;
            infinite_siren |= read_be16(b + 9) == 0;
            break;
        case COMMAND_BLINK:
            finite_blink |= read_be16(b + 7) != 0;
            infinite_blink |= read_be16(b + 7) == 0;
            break;
        case COMMAND_BREATHE:
            finite_breathe |= read_be16(b + 8) != 0;
            infinite_breathe |= read_be16(b + 8) == 0;
            break;
        case COMMAND_ALTERNATE:
            finite_alternate |= read_be16(b + 6) != 0;
            infinite_alternate |= read_be16(b + 6) == 0;
            break;
        case COMMAND_STOP:
            stop_masks |= 1u << b[1];
            break;
        }
    }
    assert(commands_seen == (1u << (COMMAND_VIBRATO + 1)) - 2);
    assert(finite_sequence && infinite_sequence && rest);
    assert(finite_siren && infinite_siren && finite_blink && infinite_blink);
    assert(finite_breathe && infinite_breathe && finite_alternate && infinite_alternate);
    assert(stop_masks == ((1u << CHANNEL_ALL) | (1u << CHANNEL_RED) |
                          (1u << CHANNEL_SOUND) | (1u << CHANNEL_LEDS)));

    run_main_until(cycle_writes + 1);
    assert(elapsed_ms == cycle_ms);
    assert(writes[0].at_ms == 0);
    assert(memcmp(writes[0].bytes, all_stop, sizeof(all_stop)) == 0);
    assert(memcmp(writes[cycle_writes].bytes, all_stop, sizeof(all_stop)) == 0);
    assert(writes[cycle_writes].at_ms == cycle_ms);

    for (unsigned partial = 0; partial < 2; ++partial) {
        short_write = partial != 0;
        for (size_t i = 0; i < cycle_writes; ++i) {
            write_count = elapsed_ms = 0;
            fail_at = i;
            assert(!test_cycle());
            assert(write_count == fail_at + 1);
            run_main_until(fail_at + 1 + cycle_writes + 1);
            const size_t retry = fail_at + 1;
            assert(writes[retry].size == sizeof(all_stop));
            assert(memcmp(writes[retry].bytes, all_stop, sizeof(all_stop)) == 0);
            assert(writes[retry].at_ms == writes[fail_at].at_ms + 1000);
            assert(writes[retry + cycle_writes].at_ms ==
                   writes[retry].at_ms + cycle_ms);
            assert(memcmp(writes[retry + cycle_writes].bytes, all_stop, sizeof(all_stop)) == 0);
        }
        fail_all_writes = true;
        run_main_until(4);
        assert(elapsed_ms == 4000);
        for (size_t i = 0; i < write_count; ++i) {
            assert(writes[i].size == sizeof(all_stop));
            assert(memcmp(writes[i].bytes, all_stop, sizeof(all_stop)) == 0);
            assert(writes[i].at_ms == i * 1000);
        }
        fail_all_writes = false;
    }
    puts("Pico autonomous effects, headless startup and I2C retry tests passed.");
    return 0;
}
