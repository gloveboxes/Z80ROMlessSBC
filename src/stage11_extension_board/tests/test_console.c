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
    assert(!running_main || elapsed_ms < 60000);
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
    assert(size == command_length(bytes[0]));
    write_t *write = &writes[write_count++];
    memcpy(write->bytes, bytes, size);
    write->size = size;
    write->at_ms = elapsed_ms;
    if (fail_all_writes || write_count - 1 == fail_at) {
        return short_write ? (int)size - 1 : -1;
    }
    return (int)size;
}

static void expect_led(size_t index, uint8_t command, uint8_t value, unsigned at_ms) {
    assert(index < write_count);
    assert(writes[index].size == 2);
    assert(writes[index].bytes[0] == command && writes[index].bytes[1] == value);
    assert(writes[index].at_ms == at_ms);
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
    const uint8_t stop[] = {COMMAND_TONE, 0, 0, 0, 0};
    assert(writes[0].size == sizeof(stop));
    assert(memcmp(writes[0].bytes, stop, sizeof(stop)) == 0);
    expect_led(1, COMMAND_BLUE, 0, 0);
    expect_led(2, COMMAND_RED, 255, 0);
    expect_led(3, COMMAND_RED, 0, 1000);
    expect_led(4, COMMAND_BLUE, 255, 1000);
    expect_led(5, COMMAND_RED, 255, 2000);
    assert(write_count == 6 + 2 * 511);
    assert(elapsed_ms == 3000 + 4 * 511);
    for (unsigned step = 0; step <= 510; ++step) {
        uint8_t level = (uint8_t)(step <= 255 ? step : 510 - step);
        expect_led(6 + step * 2, COMMAND_RED, level, 3000 + step * 4);
        expect_led(7 + step * 2, COMMAND_BLUE, level, 3000 + step * 4);
    }
    const size_t failures[] = {0, 1, 2, 3, 4, 5, 6, 7, 516, 517, 1026, 1027};
    for (unsigned partial = 0; partial < 2; ++partial) {
        for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
            write_count = elapsed_ms = 0;
            fail_at = failures[i];
            short_write = partial != 0;
            assert(!test_led_channels());
            assert(write_count == fail_at + 1);
        }
    }

    write_count = elapsed_ms = 0;
    fail_at = SIZE_MAX;
    assert(test_tone_sweep());
    const unsigned steps = (SWEEP_END_HZ - SWEEP_START_HZ) / SWEEP_STEP_HZ + 1;
    assert(steps == 17 && write_count == 2 + steps);
    expect_led(0, COMMAND_RED, 0, 0);
    expect_led(1, COMMAND_BLUE, 0, 0);
    for (unsigned i = 0; i < steps; ++i) {
        const write_t *w = &writes[2 + i];
        unsigned hz = SWEEP_START_HZ + i * SWEEP_STEP_HZ;
        assert(w->size == 5 && w->bytes[0] == COMMAND_TONE);
        assert(read_be16(w->bytes + 1) == hz);
        assert(read_be16(w->bytes + 3) == SWEEP_TONE_MS);
        assert(w->at_ms == i * SWEEP_STEP_MS);
        assert(tone_config((uint16_t)hz).clock_select != 0);
    }
    assert(SWEEP_TONE_MS < SWEEP_STEP_MS);
    for (size_t fail = 0; fail < 2 + steps; fail += 6) {
        write_count = elapsed_ms = 0;
        fail_at = fail;
        assert(!test_tone_sweep());
        assert(write_count == fail + 1);
    }
    write_count = elapsed_ms = 0;
    fail_at = SIZE_MAX;
    assert(test_cycle());
    const size_t cycle_writes = write_count;
    const unsigned cycle_ms = elapsed_ms;

    run_main_until(cycle_writes + 1);
    assert(elapsed_ms == cycle_ms);
    assert(writes[0].at_ms == 0);
    assert(memcmp(writes[0].bytes, stop, sizeof(stop)) == 0);
    assert(memcmp(writes[cycle_writes].bytes, stop, sizeof(stop)) == 0);
    assert(writes[cycle_writes].at_ms == cycle_ms);

    const size_t retry_failures[] = {0, 517, 1030, cycle_writes - 1};
    for (unsigned partial = 0; partial < 2; ++partial) {
        short_write = partial != 0;
        for (size_t i = 0; i < sizeof(retry_failures) / sizeof(retry_failures[0]); ++i) {
            fail_at = retry_failures[i];
            run_main_until(fail_at + 1 + cycle_writes + 1);
            const size_t retry = fail_at + 1;
            assert(writes[retry].size == sizeof(stop));
            assert(memcmp(writes[retry].bytes, stop, sizeof(stop)) == 0);
            assert(writes[retry].at_ms == writes[fail_at].at_ms + 1000);
            assert(writes[retry + cycle_writes].at_ms ==
                   writes[retry].at_ms + cycle_ms);
            assert(memcmp(writes[retry + cycle_writes].bytes, stop, sizeof(stop)) == 0);
        }
        fail_all_writes = true;
        run_main_until(4);
        assert(elapsed_ms == 4000);
        for (size_t i = 0; i < write_count; ++i) {
            assert(writes[i].size == sizeof(stop));
            assert(memcmp(writes[i].bytes, stop, sizeof(stop)) == 0);
            assert(writes[i].at_ms == i * 1000);
        }
        fail_all_writes = false;
    }
    puts("Pico LED/tone timing, headless startup and I2C retry tests passed.");
    return 0;
}
