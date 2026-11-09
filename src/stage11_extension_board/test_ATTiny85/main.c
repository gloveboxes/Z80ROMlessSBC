#include <stdio.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "../flash_ATTiny85/target/protocol.h"

#define PIN_SDA 20u
#define PIN_SCL 21u
#define BE16(value) (uint8_t)((value) >> 8), (uint8_t)(value)

static const struct {
    const char *name;
    uint8_t packet[COMMAND_MAX_BYTES];
    uint16_t wait_ms;
} arcade_effects[] = {
    {"Coin pickup", {
        COMMAND_SEQUENCE, 2, BE16(1),
        BE16(2600), BE16(70), BE16(3900), BE16(180)
    }, 450},
    {"Jump", {
        COMMAND_CUE, BE16(800), BE16(2800), BE16(220), 0, 0, 255, 0
    }, 420},
    {"Ricochet", {
        COMMAND_SEQUENCE, 6, BE16(1),
        BE16(4000), BE16(40), BE16(1800), BE16(50),
        BE16(3500), BE16(40), BE16(1400), BE16(60),
        BE16(2800), BE16(40), BE16(900), BE16(100)
    }, 530},
    {"Teleport", {
        COMMAND_SEQUENCE, 4, BE16(3),
        BE16(1200), BE16(45), BE16(2400), BE16(45),
        BE16(3600), BE16(45), BE16(0), BE16(45)
    }, 740},
    {"Shield recharge", {
        COMMAND_CUE, BE16(1200), BE16(4200), BE16(750), 0, 0, 0, 255
    }, 950},
    {"Extra life", {
        COMMAND_SEQUENCE, 6, BE16(1),
        BE16(1568), BE16(90), BE16(2093), BE16(90),
        BE16(2637), BE16(90), BE16(3136), BE16(90),
        BE16(0), BE16(80), BE16(4186), BE16(280)
    }, 920},
    {"Victory fanfare", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(2093), BE16(100), BE16(0), BE16(40),
        BE16(2093), BE16(100), BE16(2637), BE16(160),
        BE16(3136), BE16(160), BE16(0), BE16(60),
        BE16(2637), BE16(120), BE16(4186), BE16(400)
    }, 1340},
    {"Game over", {
        COMMAND_SEQUENCE, 5, BE16(1),
        BE16(2400), BE16(180), BE16(2000), BE16(180),
        BE16(1600), BE16(180), BE16(1200), BE16(180),
        BE16(600), BE16(400)
    }, 1320},
    {"Do re mi fa sol la ti do (C6-C7)", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1047), BE16(250), BE16(1175), BE16(250),
        BE16(1319), BE16(250), BE16(1397), BE16(250),
        BE16(1568), BE16(250), BE16(1760), BE16(250),
        BE16(1976), BE16(250), BE16(2093), BE16(500)
    }, 2450},
    {"Maze: pellet munching", {
        COMMAND_SEQUENCE, 4, BE16(3),
        BE16(1600), BE16(40), BE16(900), BE16(40),
        BE16(1900), BE16(40), BE16(0), BE16(40)
    }, 680},
    {"Maze: power pellet", {
        COMMAND_SEQUENCE, 4, BE16(3),
        BE16(1100), BE16(60), BE16(2200), BE16(60),
        BE16(3300), BE16(60), BE16(0), BE16(20)
    }, 800},
    {"Maze: enemy captured", {
        COMMAND_SWEEP, BE16(700), BE16(4200), BE16(240)
    }, 440},
    {"Bricks: paddle bounce", {
        COMMAND_SEQUENCE, 2, BE16(1),
        BE16(900), BE16(20), BE16(1400), BE16(35)
    }, 255},
    {"Bricks: wall bounce", {
        COMMAND_SEQUENCE, 1, BE16(1),
        BE16(2300), BE16(35)
    }, 235},
    {"Bricks: brick shattered", {
        COMMAND_SEQUENCE, 3, BE16(1),
        BE16(3800), BE16(25), BE16(2900), BE16(25),
        BE16(1800), BE16(50)
    }, 300},
    {"Bricks: ball lost", {
        COMMAND_CUE, BE16(1800), BE16(300), BE16(650), 255, 0, 0, 0
    }, 850},
    {"Invader footsteps: accelerating", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1100), BE16(70), BE16(0), BE16(230),
        BE16(950), BE16(70), BE16(0), BE16(160),
        BE16(800), BE16(60), BE16(0), BE16(90),
        BE16(650), BE16(50), BE16(0), BE16(40)
    }, 970},
    {"UFO flyby", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1000), BE16(120), BE16(1800), BE16(120),
        BE16(1400), BE16(120), BE16(2600), BE16(120),
        BE16(2200), BE16(120), BE16(3200), BE16(120),
        BE16(1800), BE16(120), BE16(900), BE16(120)
    }, 1160},
    {"Robot chatter", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(3200), BE16(35), BE16(0), BE16(20),
        BE16(1100), BE16(55), BE16(4100), BE16(30),
        BE16(0), BE16(25), BE16(1700), BE16(70),
        BE16(3600), BE16(40), BE16(0), BE16(90)
    }, 565},
    {"Engine: accelerating", {
        COMMAND_SWEEP, BE16(180), BE16(1600), BE16(900)
    }, 1100},
    {"Engine: high-speed warble", {
        COMMAND_SIREN, BE16(1500), BE16(1900), BE16(30), BE16(30), BE16(800)
    }, 1000},
    {"Bomb countdown: accelerating", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(3000), BE16(40), BE16(0), BE16(300),
        BE16(3000), BE16(40), BE16(0), BE16(180),
        BE16(3000), BE16(40), BE16(0), BE16(90),
        BE16(3600), BE16(40), BE16(0), BE16(40)
    }, 970},
    {"Explosion / player death: tonal crash", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(3600), BE16(25), BE16(1700), BE16(30),
        BE16(2900), BE16(35), BE16(1100), BE16(40),
        BE16(2100), BE16(45), BE16(650), BE16(60),
        BE16(950), BE16(90), BE16(180), BE16(180)
    }, 705},
    {"Door unlocking: latch chirps", {
        COMMAND_SEQUENCE, 6, BE16(1),
        BE16(1800), BE16(50), BE16(0), BE16(50),
        BE16(2400), BE16(50), BE16(0), BE16(50),
        BE16(3000), BE16(50), BE16(0), BE16(50)
    }, 500},
    {"Door unlocking: open", {
        COMMAND_SWEEP, BE16(1000), BE16(3200), BE16(300)
    }, 500},
    {"Radar / sonar ping", {
        COMMAND_SEQUENCE, 2, BE16(2),
        BE16(3800), BE16(100), BE16(0), BE16(700)
    }, 1800},
    {"Low-health heartbeat", {
        COMMAND_SEQUENCE, 4, BE16(3),
        BE16(250), BE16(120), BE16(0), BE16(60),
        BE16(210), BE16(140), BE16(0), BE16(680)
    }, 3200},
    {"Combo multiplier: rising hit chain", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1200), BE16(40), BE16(1500), BE16(40),
        BE16(1800), BE16(40), BE16(2200), BE16(40),
        BE16(2600), BE16(40), BE16(3100), BE16(40),
        BE16(3700), BE16(40), BE16(4400), BE16(160)
    }, 640},
    {"Treasure discovered", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1568), BE16(60), BE16(1976), BE16(60),
        BE16(2349), BE16(60), BE16(3136), BE16(60),
        BE16(1976), BE16(60), BE16(2349), BE16(60),
        BE16(3136), BE16(60), BE16(3951), BE16(300)
    }, 920},
    {"Level complete", {
        COMMAND_SEQUENCE, 8, BE16(1),
        BE16(1760), BE16(100), BE16(2217), BE16(80),
        BE16(2637), BE16(140), BE16(0), BE16(60),
        BE16(2349), BE16(100), BE16(2637), BE16(100),
        BE16(3520), BE16(180), BE16(4435), BE16(360)
    }, 1320},
    {"Primitive: smooth segmented engine envelope", {
        COMMAND_SEGMENTS, 4, BE16(1), BE16(300),
        BE16(1800), BE16(600), BE16(2600), BE16(200),
        BE16(1400), BE16(200), BE16(3200), BE16(400)
    }, 1600},
    {"Primitive: repeating segmented ricochet", {
        COMMAND_SEGMENTS, 3, BE16(3), BE16(4000),
        BE16(1200), BE16(90), BE16(3000), BE16(60), BE16(800), BE16(150)
    }, 1100},
    {"Primitive: twelve accelerating countdown pulses", {
        COMMAND_PULSES, 12, BE16(3000), BE16(40), BE16(240), BE16(20)
    }, 2110},
    {"Primitive: eight slowing radar pulses", {
        COMMAND_PULSES, 8, BE16(3800), BE16(60), BE16(20), BE16(200)
    }, 1450},
    {"Primitive: rising UFO with vibrato", {
        COMMAND_VIBRATO, BE16(1200), BE16(3200), BE16(1500), BE16(180), BE16(120)
    }, 1700},
    {"Primitive: descending engine with vibrato", {
        COMMAND_VIBRATO, BE16(2400), BE16(700), BE16(1200), BE16(100), BE16(80)
    }, 1400}
};

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

static bool stop(uint8_t channels) {
    const uint8_t packet[] = {COMMAND_STOP, channels};
    return send_command(packet, sizeof(packet));
}

static bool fade(uint8_t mask, uint8_t from, uint8_t to, uint16_t ms) {
    const uint8_t packet[] = {COMMAND_FADE, mask, from, to, BE16(ms)};
    return send_command(packet, sizeof(packet));
}

static bool sweep(uint16_t from, uint16_t to, uint16_t ms) {
    const uint8_t packet[] = {COMMAND_SWEEP, BE16(from), BE16(to), BE16(ms)};
    return send_command(packet, sizeof(packet));
}

static bool siren(uint16_t total_ms) {
    const uint8_t packet[] = {
        COMMAND_SIREN, BE16(1500), BE16(3500), BE16(300), BE16(500), BE16(total_ms)
    };
    return send_command(packet, sizeof(packet));
}

static bool blink(uint8_t mask, uint16_t repeats) {
    const uint8_t packet[] = {
        COMMAND_BLINK, mask, 255, BE16(120), BE16(180), BE16(repeats)
    };
    return send_command(packet, sizeof(packet));
}

static bool breathe(uint16_t total_ms) {
    const uint8_t packet[] = {
        COMMAND_BREATHE, CHANNEL_LEDS, 16, 255,
        BE16(600), BE16(900), BE16(total_ms), 0
    };
    return send_command(packet, sizeof(packet));
}

static bool alternate(uint16_t repeats) {
    const uint8_t packet[] = {
        COMMAND_ALTERNATE, 255, BE16(200), BE16(300), BE16(repeats)
    };
    return send_command(packet, sizeof(packet));
}

static bool sequence(uint16_t repeats) {
    const uint8_t packet[] = {
        COMMAND_SEQUENCE, 4, BE16(repeats),
        BE16(2000), BE16(180), BE16(0), BE16(100),
        BE16(2500), BE16(180), BE16(3200), BE16(350)
    };
    return send_command(packet, sizeof(packet));
}

static bool test_led_channels(void) {
    printf("LED-only check (tone OFF): red only, fully on for 1 second.\n");
    if (!stop(CHANNEL_ALL) || !led(COMMAND_RED, 255)) {
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
    printf("LED-only check: autonomous fade up, then down; final level held.\n");
    if (!fade(CHANNEL_LEDS, 0, 255, 1000)) {
        return false;
    }
    sleep_ms(1200);
    if (!fade(CHANNEL_LEDS, 255, 0, 1000)) {
        return false;
    }
    sleep_ms(1200);
    return true;
}

static bool test_arcade_effects(void) {
    for (unsigned i = 0; i < sizeof(arcade_effects) / sizeof(arcade_effects[0]); ++i) {
        printf("Arcade effect: %s.\n", arcade_effects[i].name);
        const uint8_t *packet = arcade_effects[i].packet;
        size_t size = packet_length(packet);
        if (!send_command(packet, size)) {
            return false;
        }
        sleep_ms(arcade_effects[i].wait_ms);
    }
    return stop(CHANNEL_ALL);
}

static bool test_effects(void) {
    printf("Autonomous descending laser, then ascending power-up.\n");
    if (!sweep(4000, 1000, 300)) {
        return false;
    }
    sleep_ms(500);
    if (!sweep(1000, 4000, 600)) {
        return false;
    }
    sleep_ms(800);
    printf("Finite siren plus alternating LEDs; both finish automatically.\n");
    if (!siren(2400) || !alternate(5)) {
        return false;
    }
    sleep_ms(2700);
    printf("Jingle with a rest, repeated twice; both LEDs breathe independently.\n");
    if (!sequence(2) || !breathe(3000)) {
        return false;
    }
    sleep_ms(3200);
    printf("Red blinks three times while blue fades; sound is silent.\n");
    if (!blink(CHANNEL_RED, 3) || !fade(CHANNEL_BLUE, 255, 0, 1200)) {
        return false;
    }
    sleep_ms(1400);
    printf("Single synchronized cue: rising pitch and opposing LED fades.\n");
    const uint8_t cue[] = {
        COMMAND_CUE, BE16(1500), BE16(4000), BE16(1200), 0, 255, 255, 0
    };
    if (!send_command(cue, sizeof(cue))) {
        return false;
    }
    sleep_ms(1400);
    printf("Indefinite siren/breathe; stop sound only, then red only.\n");
    if (!siren(0) || !breathe(0)) {
        return false;
    }
    sleep_ms(1200);
    if (!stop(CHANNEL_SOUND)) {
        return false;
    }
    sleep_ms(500);
    if (!stop(CHANNEL_RED)) {
        return false;
    }
    sleep_ms(500);
    printf("Replace blue breathing with blink, then replace it with a fixed level.\n");
    if (!blink(CHANNEL_BLUE, 0)) {
        return false;
    }
    sleep_ms(1000);
    if (!led(COMMAND_BLUE, 128) || !sequence(0)) {
        return false;
    }
    sleep_ms(1000);
    printf("Replace indefinite sequence with a timed tone, then explicit tone stop.\n");
    if (!tone(4000, 300)) {
        return false;
    }
    sleep_ms(500);
    if (!tone(2000, 2000)) {
        return false;
    }
    sleep_ms(100);
    if (!tone(0, 0) || !alternate(0)) {
        return false;
    }
    sleep_ms(700);
    if (!stop(CHANNEL_LEDS) || !siren(0) || !blink(CHANNEL_LEDS, 0)) {
        return false;
    }
    sleep_ms(500);
    printf("All-stop: silence and both LEDs off.\n");
    if (!stop(CHANNEL_ALL)) {
        return false;
    }
    sleep_ms(1000);
    return true;
}

static bool test_cycle(void) {
    return test_led_channels() && test_arcade_effects() && test_effects();
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
