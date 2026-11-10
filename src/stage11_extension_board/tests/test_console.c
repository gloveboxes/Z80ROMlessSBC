#include <assert.h>
#include <setjmp.h>
#include <string.h>

#define main attiny_firmware_main
#include "../flash_ATTiny85/target/main.c"
#undef main
#define main pico_test_main
#include "../test_ATTiny85/main.c"
#undef main

static unsigned elapsed_ms, writes, reads;
static unsigned fail_write, fail_read;
static bool short_transfer, frozen_worker, wrong_version, suppress_ack, running_main;
static unsigned stdio_calls, gpio_calls, init_calls;
static jmp_buf main_exit;
static unsigned effects_seen;
static bool presets_seen[PRESET_COUNT];
static bool manual_scale_seen;
static unsigned results_seen, controls_seen;

static void pump(void) {
    service_start();
    service_effects();
    if (!frozen_worker)
        service_command();
}

static void reset_fixture(void) {
    memset(&sound, 0, sizeof(sound));
    command_pending = start_pending = packet_size = expected_size = 0;
    event_count = event_head = event_overflow = playback_count = 0;
    event_sequence = completed_count = rejected_count = 0;
    milliseconds = serviced_ms = 0;
    request_id = request_result = 0;
    next_id = 0;
    elapsed_ms = writes = reads = 0;
    fail_write = fail_read = 0;
    frozen_worker = wrong_version = suppress_ack = false;
    effects_seen = results_seen = controls_seen = 0;
    memset(presets_seen, 0, sizeof(presets_seen));
    manual_scale_seen = false;
    tone_stop();
    publish_status();
}

void sleep_ms(unsigned ms) {
    elapsed_ms += ms;
    assert(elapsed_ms < 180000);
    milliseconds += ms;
    pump();
}
void stdio_init_all(void) { ++stdio_calls; }
void tight_loop_contents(void) { assert(false); }
void gpio_set_function(unsigned pin, unsigned function) {
    assert((pin == PIN_SDA || pin == PIN_SCL) && function == GPIO_FUNC_I2C);
    ++gpio_calls;
}
unsigned i2c_init(void *i2c, unsigned baud) {
    assert(i2c == i2c0 && baud == 400000);
    ++init_calls;
    return baud;
}

static void begin_transfer(bool read) {
    pump();
    PINB = SDA_MASK;
    USI_START_vect();
    service_start();
    USIDR = (ATTINY85_I2C_ADDRESS << 1) | read;
    USI_OVF_vect();
    assert(receive_state == ADDRESS_ACK);
    USI_OVF_vect();
}

int i2c_write_timeout_us(void *i2c, uint8_t address, const uint8_t *b,
                         size_t size, bool nostop, unsigned timeout_us) {
    assert(i2c == i2c0 && address == ATTINY85_I2C_ADDRESS && !nostop && timeout_us == 50000);
    assert(size == packet_length(b) && command_valid(b));
    if (running_main && writes == 1)
        longjmp(main_exit, 1);
    ++writes;
    if (fail_write == writes)
        return short_transfer ? (int)size - 1 : -1;
    if (suppress_ack && b[0] == COMMAND_ACK_EVENT)
        return size;
    controls_seen |= 1u << (b[0] - COMMAND_PLAY);
    if (b[0] == COMMAND_PLAY) {
        effects_seen |= 1u << b[6];
        if (b[6] == FX_PRESET)
            presets_seen[b[7]] = true;
        if (b[6] == FX_SEQUENCE && b[7] == 8 && read_be16(b + 10) == 1047) {
            const uint8_t scale[] = {
                FX_SEQUENCE, 8, BE16(1),
                BE16(1047), BE16(250), BE16(1175), BE16(250), BE16(1319), BE16(250),
                BE16(1397), BE16(250), BE16(1568), BE16(250), BE16(1760), BE16(250),
                BE16(1976), BE16(250), BE16(2093), BE16(500)
            };
            assert(size == 6 + sizeof(scale) && memcmp(b + 6, scale, sizeof(scale)) == 0);
            manual_scale_seen = true;
        }
    }
    begin_transfer(false);
    for (size_t i = 0; i < size; ++i) {
        USIDR = b[i];
        USI_OVF_vect();
        assert(receive_state == (i == size - 1 ? FINAL_ACK : DATA_ACK));
        USI_OVF_vect();
    }
    /* Foreground execution happens later, not in the write ACK. */
    assert(command_pending);
    return size;
}

int i2c_read_timeout_us(void *i2c, uint8_t address, uint8_t *b,
                        size_t size, bool nostop, unsigned timeout_us) {
    assert(i2c == i2c0 && address == ATTINY85_I2C_ADDRESS && !nostop && timeout_us == 50000);
    assert(size == STATUS_BYTES);
    ++reads;
    if (fail_read == reads)
        return short_transfer ? (int)size - 1 : -1;
    begin_transfer(true);
    for (size_t i = 0; i < size; ++i) {
        assert(receive_state == TRANSMIT);
        b[i] = USIDR;
        USI_OVF_vect();
        assert(receive_state == TRANSMIT_ACK);
        USIDR = i == size - 1 ? 1 : 0;
        USI_OVF_vect();
    }
    if (wrong_version)
        b[S_VERSION] = 0xff;
    if (b[S_RESULT] <= RESULT_INVALID)
        results_seen |= 1u << b[S_RESULT];
    return size;
}

int main(void) {
    reset_fixture();
    assert(test_cycle());
    unsigned cycle_writes = writes, cycle_reads = reads;
    assert(cycle_writes < 4u * PRESET_COUNT + 120u && cycle_reads > cycle_writes);
    assert(effects_seen == (1u << (FX_PRESET + 1)) - 2);
    for (unsigned id = 1; id < PRESET_COUNT; ++id)
        assert(presets_seen[id]);
    assert(manual_scale_seen);
    assert(controls_seen == 31);
    assert(results_seen & (1u << RESULT_PRIORITY));
    assert(results_seen & (1u << RESULT_QUEUE_FULL));
    assert(results_seen & (1u << RESULT_NO_MATCH));
    assert(!sound.mode && !playback_count && !event_count);

    /* Every write, including event ACKs, must propagate transport failures. */
    for (unsigned partial = 0; partial < 2; ++partial) {
        short_transfer = partial != 0;
        for (unsigned fail = 1; fail <= cycle_writes; ++fail) {
            reset_fixture();
            fail_write = fail;
            assert(!test_cycle() && writes == fail);
        }
        const unsigned read_failures[] = {1, 2, 10, cycle_reads / 2, cycle_reads};
        for (unsigned i = 0; i < sizeof(read_failures) / sizeof(read_failures[0]); ++i) {
            reset_fixture();
            fail_read = read_failures[i];
            assert(!test_cycle() && reads == fail_read);
        }
    }
    reset_fixture();
    frozen_worker = true;
    assert(!stop()); /* ACK is not execution confirmation. */
    reset_fixture();
    wrong_version = true;
    assert(!stop());
    reset_fixture();
    suppress_ack = true;
    assert(!preset(PRESET_COIN, 1, 0, 100, 100, RESULT_ACCEPTED));
    reset_fixture();
    for (unsigned i = 0; i < EVENT_CAPACITY + 1; ++i)
        event(1, EVENT_STARTED);
    publish_status();
    assert(read_status() && !drain_events());
    assert(!event_count && !event_overflow);
    assert(stop()); /* Overflow recovery must not trap the retry loop. */
    reset_fixture();
    assert(preset(PRESET_ENGINE, 1, 0, 100, 100, RESULT_ACCEPTED));
    assert(!wait_idle()); /* Indefinite sound has a bounded demo timeout. */
    reset_fixture();
    fail_write = 1;
    assert(!test_cycle());
    fail_write = 0;
    assert(test_cycle()); /* Next cycle recovers without target reset. */

    reset_fixture();
    stdio_calls = gpio_calls = init_calls = 0;
    running_main = true;
    if (setjmp(main_exit) == 0)
        pico_test_main();
    running_main = false;
    assert(stdio_calls == 1 && gpio_calls == 2 && init_calls == 1);
    assert(writes == 1); /* Demo starts without waiting for a USB terminal. */
    puts("Actual target/Pico I2C integration, status-driven demo and failure checks passed.");
    return 0;
}
