#include <stdio.h>
#include <string.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "../flash_ATTiny85/target/protocol.h"

#define PIN_SDA 20u
#define PIN_SCL 21u
#define BE16(value) (uint8_t)((value) >> 8), (uint8_t)(value)

static uint8_t next_id;
static uint8_t status[STATUS_BYTES];

static uint8_t new_id(void) {
    if (++next_id == 0)
        ++next_id;
    return next_id;
}

static bool send_command(const uint8_t *packet, size_t size) {
    int result = i2c_write_timeout_us(i2c0, ATTINY85_I2C_ADDRESS,
                                     packet, size, false, 50000);
    if (result != (int)size) {
        printf("ERROR: command %02x I2C write %d, expected %u.\n",
               packet[0], result, (unsigned)size);
        return false;
    }
    return true;
}

static bool read_status(void) {
    int result = i2c_read_timeout_us(i2c0, ATTINY85_I2C_ADDRESS,
                                    status, sizeof(status), false, 50000);
    if (result != (int)sizeof(status)) {
        printf("ERROR: status I2C read %d, expected %u.\n", result, (unsigned)sizeof(status));
        return false;
    }
    if (status[S_VERSION] != PROTOCOL_VERSION) {
        printf("ERROR: status protocol %u, expected %u; check ATtiny firmware.\n",
               status[S_VERSION], PROTOCOL_VERSION);
        return false;
    }
    return true;
}

static bool drain_events(void) {
    bool overflow = (status[S_FLAGS] & STATUS_EVENT_OVERFLOW) != 0;
    if (overflow) {
        printf("ERROR: sound event overflow; playback counters may include lost events.\n");
    }
    for (unsigned count = 0; count < 32; ++count) {
        if (!status[S_EVENT_TYPE])
            return !overflow;
        uint16_t sequence = read_be16(status + S_EVENT_SEQ_HI);
        printf("  event=%u request=%u sequence=%u\n",
               status[S_EVENT_TYPE], status[S_EVENT_ID], sequence);
        const uint8_t ack[] = {COMMAND_ACK_EVENT, BE16(sequence)};
        if (!send_command(ack, sizeof(ack)))
            return false;
        bool consumed = false;
        for (unsigned retry = 0; retry < 50; ++retry) {
            sleep_ms(2);
            if (!read_status())
                return false;
            if (!(status[S_FLAGS] & STATUS_COMMAND_PENDING) &&
                (!status[S_EVENT_TYPE] ||
                 read_be16(status + S_EVENT_SEQ_HI) != sequence)) {
                consumed = true;
                break;
            }
        }
        if (!consumed) {
            printf("ERROR: sound event acknowledgement timed out.\n");
            return false;
        }
        if (status[S_FLAGS] & STATUS_EVENT_OVERFLOW) {
            if (!overflow)
                printf("ERROR: sound events overflowed while draining.\n");
            overflow = true;
        }
    }
    printf("ERROR: sound events did not drain within the bounded read budget.\n");
    return false;
}

static bool request(uint8_t *packet, size_t size, uint8_t expected) {
    packet[1] = new_id();
    if (!send_command(packet, size))
        return false;
    for (unsigned retry = 0; retry < 50; ++retry) {
        sleep_ms(2);
        if (!read_status())
            return false;
        if (!(status[S_FLAGS] & STATUS_COMMAND_PENDING) &&
            status[S_REQUEST_ID] == packet[1]) {
            if (status[S_RESULT] != expected) {
                printf("ERROR: request %u result %u, expected %u.\n",
                       packet[1], status[S_RESULT], expected);
                return false;
            }
            return drain_events();
        }
    }
    printf("ERROR: request %u execution timed out.\n", packet[1]);
    return false;
}

static bool wait_idle(void) {
    for (unsigned elapsed = 0; elapsed < 15000; elapsed += 10) {
        if (!read_status() || !drain_events())
            return false;
        if (!(status[S_FLAGS] & STATUS_PLAYING) && !status[S_QUEUED])
            return true;
        sleep_ms(10);
    }
    printf("ERROR: sound did not complete within 15 seconds.\n");
    return false;
}

static bool stop(void) {
    uint8_t packet[] = {COMMAND_STOP, 0};
    return request(packet, sizeof(packet), RESULT_STOPPED);
}

static bool preset(uint8_t effect, uint8_t priority, uint8_t flags,
                   uint8_t pitch, uint8_t speed, uint8_t expected) {
    uint8_t packet[] = {COMMAND_PLAY, 0, priority, flags, pitch, speed, FX_PRESET, effect};
    return request(packet, sizeof(packet), expected);
}

static bool play_effect(const uint8_t *effect) {
    uint8_t packet[COMMAND_MAX_BYTES] = {COMMAND_PLAY, 0, 2, 0, 100, 100};
    memcpy(packet + 6, effect, effect_length(effect));
    return request(packet, packet_length(packet), RESULT_ACCEPTED);
}

static bool test_primitives(void) {
    static const struct {
        const char *name;
        uint8_t effect[36];
    } effects[] = {
        {"timed tone", {FX_TONE, BE16(2000), BE16(100)}},
        {"power-up sweep", {FX_SWEEP, BE16(1000), BE16(4000), BE16(300)}},
        {"finite siren", {FX_SIREN, BE16(1500), BE16(3500), BE16(100), BE16(150), BE16(750)}},
        {"notes and rests", {FX_SEQUENCE, 4, BE16(2),
            BE16(2000), BE16(80), BE16(0), BE16(60), BE16(2500), BE16(80), BE16(3200), BE16(120)}},
        {"do re mi manual note sequence", {FX_SEQUENCE, 8, BE16(1),
            BE16(1047), BE16(250), BE16(1175), BE16(250), BE16(1319), BE16(250),
            BE16(1397), BE16(250), BE16(1568), BE16(250), BE16(1760), BE16(250),
            BE16(1976), BE16(250), BE16(2093), BE16(500)}},
        {"joined pitch envelope", {FX_SEGMENTS, 4, BE16(1), BE16(300),
            BE16(1800), BE16(600), BE16(2600), BE16(200),
            BE16(1400), BE16(200), BE16(3200), BE16(400)}},
        {"accelerating countdown", {FX_PULSES, 12, BE16(3000), BE16(40), BE16(240), BE16(20)}},
        {"slowing radar", {FX_PULSES, 8, BE16(3800), BE16(60), BE16(20), BE16(200)}},
        {"rising UFO vibrato", {FX_VIBRATO, BE16(1200), BE16(3200), BE16(1500), BE16(180), BE16(120)}},
        {"major arpeggio", {FX_ARPEGGIO, BE16(1047), BE16(100), BE16(1200), 0}},
        {"minor arpeggio", {FX_ARPEGGIO, BE16(1047), BE16(100), BE16(1200), 1}},
        {"gated machine-gun buzz", {FX_GATE, BE16(800), BE16(30), BE16(50), BE16(800)}}
    };
    for (unsigned i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i) {
        printf("Primitive: %s.\n", effects[i].name);
        if (!play_effect(effects[i].effect) || !wait_idle())
            return false;
        sleep_ms(200);
    }
    return true;
}

static bool test_feedback(void) {
    printf("Priority: engine interrupted by alarm; low priority rejected; equal replaces.\n");
    if (!preset(PRESET_ENGINE, 1, 0, 100, 100, RESULT_ACCEPTED) ||
        !preset(PRESET_ALARM, 5, 0, 100, 100, RESULT_ACCEPTED) ||
        !preset(PRESET_COIN, 1, 0, 100, 100, RESULT_PRIORITY) ||
        !preset(PRESET_LASER, 5, 0, 100, 100, RESULT_ACCEPTED) || !wait_idle())
        return false;

    printf("Playback FIFO: queue two sounds behind alarm; reject third; wait for completion.\n");
    uint16_t completed = read_be16(status + S_COMPLETED_HI);
    if (!preset(PRESET_ALARM, 5, 0, 100, 100, RESULT_ACCEPTED) ||
        !preset(PRESET_COIN, 1, PLAY_ENQUEUE, 100, 100, RESULT_QUEUED) ||
        !preset(PRESET_LASER, 1, PLAY_ENQUEUE, 100, 100, RESULT_QUEUED) ||
        !preset(PRESET_ROBOT, 1, PLAY_ENQUEUE, 100, 100, RESULT_QUEUE_FULL) ||
        !wait_idle())
        return false;
    if ((uint16_t)(read_be16(status + S_COMPLETED_HI) - completed) != 3) {
        printf("ERROR: playback FIFO did not complete three sounds.\n");
        return false;
    }

    printf("Live engine: change pitch and speed without restarting playback.\n");
    if (!preset(PRESET_ENGINE, 1, 0, 100, 100, RESULT_ACCEPTED))
        return false;
    uint8_t engine_id = status[S_ACTIVE_ID];
    sleep_ms(500);
    uint8_t update[] = {COMMAND_UPDATE, 0, engine_id, UPDATE_PITCH | UPDATE_SPEED,
                        180, 160, 0, 0, 0, 0};
    if (!request(update, sizeof(update), RESULT_UPDATED))
        return false;
    sleep_ms(700);
    update[2] = engine_id == 255 ? 1 : engine_id + 1;
    if (!request(update, sizeof(update), RESULT_NO_MATCH) ||
        !preset(PRESET_COIN, 1, PLAY_ENQUEUE, 100, 100, RESULT_QUEUED))
        return false;
    uint8_t clear[] = {COMMAND_CLEAR_QUEUE, 0};
    if (!request(clear, sizeof(clear), RESULT_CLEARED) || !stop())
        return false;

    printf("Live vibrato: change depth/period, preserving modulation phase.\n");
    const uint8_t vibrato[] = {
        FX_VIBRATO, BE16(1200), BE16(2400), BE16(3000), BE16(80), BE16(200)
    };
    if (!play_effect(vibrato))
        return false;
    uint8_t modulation[] = {COMMAND_UPDATE, 0, status[S_ACTIVE_ID],
                            UPDATE_MODULATION, 0, 0, BE16(200), BE16(100)};
    sleep_ms(800);
    if (!request(modulation, sizeof(modulation), RESULT_UPDATED) || !wait_idle())
        return false;

    printf("Stop cancels active sound and queued playback.\n");
    return preset(PRESET_HEARTBEAT, 1, 0, 100, 100, RESULT_ACCEPTED) &&
           preset(PRESET_COIN, 1, PLAY_ENQUEUE, 100, 100, RESULT_QUEUED) &&
           stop() && wait_idle();
}

static bool test_cycle(void) {
    if (!stop() || !test_primitives())
        return false;
    static const char *const names[] = {
        "coin", "laser", "explosion", "engine", "alarm", "heartbeat",
        "treasure", "level complete", "pellet munch", "ricochet", "robot",
        "jump", "teleport", "shield", "extra life", "fanfare", "game over",
        "power pellet", "enemy capture", "paddle bounce", "wall bounce", "brick shatter",
        "ball lost", "invader footsteps", "UFO flyby", "countdown", "door unlock",
        "sonar ping", "combo multiplier",
        "player hit", "player death", "checkpoint", "item pickup",
        "menu move", "menu confirm", "menu cancel", "pause", "resume",
        "low health warning", "time running out", "enemy spawn", "enemy destroyed",
        "weapon charge", "boss appears", "falling whistle",
        "shield hit", "power-up expires", "empty weapon", "secret discovered",
        "round start", "bomb armed", "reload", "springboard", "splash", "door slam",
        "switch click", "landing thud", "skid", "warp failed", "magic cast", "freeze",
        "bonus tally", "objective failed"
    };
    _Static_assert(sizeof(names) / sizeof(names[0]) == PRESET_COUNT - 1,
                   "Every preset needs a demo label");
    for (uint8_t effect = 1; effect < PRESET_COUNT; ++effect) {
        printf("Flash preset: %s.\n", names[effect - 1]);
        if (!preset(effect, 2, 0, 100, 100, RESULT_ACCEPTED))
            return false;
        if (effect == PRESET_ENGINE || effect == PRESET_HEARTBEAT ||
            effect == PRESET_LOW_HEALTH || effect == PRESET_TIME_WARNING) {
            sleep_ms(1200);
            if (!stop())
                return false;
        } else if (!wait_idle()) {
            return false;
        }
        sleep_ms(200);
    }
    printf("Scaled coin: lower/slower, then higher/faster.\n");
    if (!preset(PRESET_COIN, 2, 0, 75, 75, RESULT_ACCEPTED) || !wait_idle() ||
        !preset(PRESET_COIN, 2, 0, 150, 150, RESULT_ACCEPTED) || !wait_idle() ||
        !test_feedback())
        return false;
    printf("Sound cycle complete; silence for one second.\n");
    sleep_ms(1000);
    return true;
}

int main(void) {
    stdio_init_all();
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    printf("\nStage 11 sound engine: I2C0 GP20 SDA, GP21 SCL, address 0x42.\n");
    for (;;) {
        if (!test_cycle()) {
            printf("ERROR: sound cycle failed; retrying from stop in one second.\n");
            sleep_ms(1000);
        }
    }
}
