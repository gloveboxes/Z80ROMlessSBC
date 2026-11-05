#include <stdio.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "../flash_ATTiny85/target/protocol.h"

#define PIN_SDA 20u
#define PIN_SCL 21u
#define SWEEP_START_HZ 1000u
#define SWEEP_END_HZ 5000u
#define SWEEP_STEP_HZ 250u
#define SWEEP_TONE_MS 400u
#define SWEEP_STEP_MS 600u

static bool send_command(const uint8_t *packet, size_t size) {
    int result = i2c_write_timeout_us(i2c0, ATTINY85_I2C_ADDRESS,
                                     packet, size, false, 50000);
    if (result != (int)size) {
        printf("ERROR: command %02x: I2C result %d, expected %u bytes. "
               "Check wiring, pull-ups and flashed firmware.\n",
               packet[0], result, (unsigned)size);
        return false;
    }
    return true;
}

static bool led(uint8_t command, uint8_t value) {
    const uint8_t packet[] = {command, value};
    return send_command(packet, sizeof(packet));
}

static bool tone(uint16_t frequency, uint16_t duration_ms) {
    const uint8_t packet[] = {COMMAND_TONE, frequency >> 8, frequency & 0xff,
                              duration_ms >> 8, duration_ms & 0xff};
    return send_command(packet, sizeof(packet));
}

static bool test_led_channels(void) {
    printf("LED-only check (tone OFF): red only, fully on for 1 second.\n");
    if (!tone(0, 0) || !led(COMMAND_BLUE, 0) || !led(COMMAND_RED, 255)) {
        return false;
    }
    sleep_ms(1000);
    printf("LED-only check: blue only, fully on for 1 second.\n");
    if (!led(COMMAND_RED, 0) || !led(COMMAND_BLUE, 255)) {
        return false;
    }
    sleep_ms(1000);
    printf("LED-only check: both fully on for 1 second; neither should flicker.\n");
    if (!led(COMMAND_RED, 255)) {
        return false;
    }
    sleep_ms(1000);
    printf("LED-only check: both fading UP, then DOWN together.\n");
    for (unsigned step = 0; step <= 510; ++step) {
        uint8_t level = (uint8_t)(step <= 255 ? step : 510 - step);
        if (!led(COMMAND_RED, level) || !led(COMMAND_BLUE, level)) {
            return false;
        }
        sleep_ms(4);
    }
    return true;
}

static bool test_tone_sweep(void) {
    printf("Piezo sweep %u-%u Hz: note the loudest frequency.\n",
           SWEEP_START_HZ, SWEEP_END_HZ);
    if (!led(COMMAND_RED, 0) || !led(COMMAND_BLUE, 0)) {
        return false;
    }
    for (unsigned hz = SWEEP_START_HZ; hz <= SWEEP_END_HZ; hz += SWEEP_STEP_HZ) {
        printf("  %u Hz\n", hz);
        if (!tone((uint16_t)hz, SWEEP_TONE_MS)) {
            return false;
        }
        sleep_ms(SWEEP_STEP_MS);
    }
    printf("Sweep complete.\n");
    return true;
}

static bool test_cycle(void) {
    if (!test_led_channels() || !test_tone_sweep() || !led(COMMAND_RED, 0) ||
        !led(COMMAND_BLUE, 0) || !tone(1000, 1500)) {
        return false;
    }
    printf("1 kHz tone for 1500 ms; fading LEDs while it plays.\n");
    for (unsigned level = 0; level <= 255; ++level) {
        if (!led(COMMAND_RED, (uint8_t)level) ||
            !led(COMMAND_BLUE, (uint8_t)(255 - level))) {
            return false;
        }
        sleep_ms(4);
    }
    sleep_ms(700);
    printf("Tone should have stopped automatically; red on, blue off.\n");
    sleep_ms(1000);
    if (!tone(440, 300) || !led(COMMAND_RED, 128) || !led(COMMAND_BLUE, 128)) {
        return false;
    }
    sleep_ms(500);
    if (!tone(880, 2000)) {
        return false;
    }
    sleep_ms(100);
    if (!tone(0, 0) || !led(COMMAND_RED, 0) || !led(COMMAND_BLUE, 0)) {
        return false;
    }
    printf("Explicit tone stop; both LEDs off.\n");
    sleep_ms(1000);
    return true;
}

int main(void) {
    stdio_init_all();
    i2c_init(i2c0, 100000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    printf("\nStage 11 ATtiny85 test: I2C0 GP20 SDA, GP21 SCL, address 0x42.\n");
    for (;;) {
        if (!test_cycle()) {
            printf("Test cycle failed. Retrying from the LED check in 1 second.\n");
            sleep_ms(1000);
        }
    }
}
