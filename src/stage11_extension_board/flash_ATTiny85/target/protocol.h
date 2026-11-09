#ifndef ATTINY85_PROTOCOL_H
#define ATTINY85_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define ATTINY85_I2C_ADDRESS 0x42u
#define PROTOCOL_VERSION 1u
#define COMMAND_PLAY 0x10u
#define COMMAND_STOP 0x11u
#define COMMAND_UPDATE 0x12u
#define COMMAND_CLEAR_QUEUE 0x13u
#define COMMAND_ACK_EVENT 0x14u
#define PLAY_ENQUEUE 1u
#define UPDATE_PITCH 1u
#define UPDATE_SPEED 2u
#define UPDATE_MODULATION 4u
#define FX_TONE 1u
#define FX_SWEEP 2u
#define FX_SIREN 3u
#define FX_SEQUENCE 4u
#define FX_SEGMENTS 5u
#define FX_PULSES 6u
#define FX_VIBRATO 7u
#define FX_ARPEGGIO 8u
#define FX_GATE 9u
#define FX_PRESET 10u
#define SEQUENCE_MAX_STEPS 8u
#define SEGMENT_MAX_STEPS 7u
#define PULSE_MAX_COUNT 64u
#define COMMAND_MAX_BYTES 42u
#define PLAYBACK_QUEUE_SIZE 2u
#define EVENT_CAPACITY 4u

enum {
    PRESET_COIN = 1, PRESET_LASER, PRESET_EXPLOSION, PRESET_ENGINE,
    PRESET_ALARM, PRESET_HEARTBEAT, PRESET_TREASURE, PRESET_LEVEL,
    PRESET_MUNCH, PRESET_RICOCHET, PRESET_ROBOT,
    PRESET_JUMP, PRESET_TELEPORT, PRESET_SHIELD, PRESET_EXTRA_LIFE,
    PRESET_FANFARE, PRESET_GAME_OVER, PRESET_POWER_PELLET, PRESET_CAPTURE,
    PRESET_PADDLE, PRESET_WALL, PRESET_BRICK, PRESET_BALL_LOST,
    PRESET_FOOTSTEPS, PRESET_UFO, PRESET_COUNTDOWN, PRESET_DOOR,
    PRESET_SONAR, PRESET_COMBO,
    PRESET_PLAYER_HIT, PRESET_PLAYER_DEATH, PRESET_CHECKPOINT, PRESET_PICKUP,
    PRESET_MENU_MOVE, PRESET_MENU_CONFIRM, PRESET_MENU_CANCEL,
    PRESET_PAUSE, PRESET_RESUME, PRESET_LOW_HEALTH, PRESET_TIME_WARNING,
    PRESET_ENEMY_SPAWN, PRESET_ENEMY_DESTROYED, PRESET_CHARGE,
    PRESET_BOSS, PRESET_FALLING,
    PRESET_SHIELD_HIT, PRESET_POWER_EXPIRES, PRESET_EMPTY_WEAPON,
    PRESET_SECRET, PRESET_ROUND_START, PRESET_BOMB_ARMED,
    PRESET_RELOAD, PRESET_SPRINGBOARD, PRESET_SPLASH, PRESET_DOOR_SLAM,
    PRESET_SWITCH, PRESET_LANDING, PRESET_SKID, PRESET_WARP_FAIL,
    PRESET_MAGIC, PRESET_FREEZE, PRESET_BONUS_TALLY, PRESET_OBJECTIVE_FAILED,
    PRESET_COUNT
};
enum {
    RESULT_NONE, RESULT_ACCEPTED, RESULT_QUEUED, RESULT_PRIORITY,
    RESULT_QUEUE_FULL, RESULT_NO_MATCH, RESULT_UPDATED, RESULT_STOPPED,
    RESULT_CLEARED, RESULT_INVALID
};
enum {
    EVENT_STARTED = 1, EVENT_QUEUED, EVENT_COMPLETED, EVENT_INTERRUPTED,
    EVENT_REJECTED, EVENT_UPDATED, EVENT_STOPPED, EVENT_CANCELLED
};
enum {
    STATUS_PLAYING = 1, STATUS_COMMAND_PENDING = 2, STATUS_EVENT_OVERFLOW = 4
};
enum {
    S_VERSION, S_FLAGS, S_ACTIVE_ID, S_PRIORITY, S_EFFECT, S_PRESET, S_QUEUED,
    S_FREQUENCY_HI, S_FREQUENCY_LO, S_REQUEST_ID, S_RESULT,
    S_EVENT_SEQ_HI, S_EVENT_SEQ_LO, S_EVENT_ID, S_EVENT_TYPE,
    S_COMPLETED_HI, S_COMPLETED_LO, S_REJECTED_HI, S_REJECTED_LO,
    STATUS_BYTES
};

static inline uint16_t read_be16(const uint8_t *b) {
    return ((uint16_t)b[0] << 8) | b[1];
}

static inline bool valid_scale(uint8_t value) {
    return value >= 25 && value <= 200;
}

static inline uint8_t effect_length(const uint8_t *b) {
    switch (b[0]) {
    case FX_TONE: return 5;
    case FX_SWEEP: return 7;
    case FX_SIREN: return 11;
    case FX_SEQUENCE: return 4u + 4u * b[1];
    case FX_SEGMENTS: return 6u + 4u * b[1];
    case FX_PULSES: return 10;
    case FX_VIBRATO: return 11;
    case FX_ARPEGGIO: return 8;
    case FX_GATE: return 9;
    case FX_PRESET: return 2;
    default: return 0;
    }
}

static inline uint8_t packet_length(const uint8_t *b) {
    switch (b[0]) {
    case COMMAND_PLAY: {
        uint8_t length = effect_length(b + 6);
        return length ? 6u + length : 0;
    }
    case COMMAND_STOP:
    case COMMAND_CLEAR_QUEUE: return 2;
    case COMMAND_UPDATE: return 10;
    case COMMAND_ACK_EVENT: return 3;
    default: return 0;
    }
}

static inline bool effect_valid(const uint8_t *b) {
    switch (b[0]) {
    case FX_TONE:
        return read_be16(b + 1) && read_be16(b + 3);
    case FX_SWEEP:
        return read_be16(b + 1) && read_be16(b + 3) && read_be16(b + 5);
    case FX_SIREN:
        return read_be16(b + 1) && read_be16(b + 3) &&
               read_be16(b + 5) && read_be16(b + 7);
    case FX_SEQUENCE:
    case FX_SEGMENTS: {
        uint8_t offset = b[0] == FX_SEQUENCE ? 4 : 6;
        uint8_t limit = b[0] == FX_SEQUENCE ? SEQUENCE_MAX_STEPS : SEGMENT_MAX_STEPS;
        if (!b[1] || b[1] > limit ||
            (b[0] == FX_SEGMENTS && !read_be16(b + 4)))
            return false;
        for (uint8_t i = 0; i < b[1]; ++i) {
            if (!read_be16(b + offset + 2 + 4u * i) ||
                (b[0] == FX_SEGMENTS && !read_be16(b + offset + 4u * i)))
                return false;
        }
        return true;
    }
    case FX_PULSES:
        return b[1] && b[1] <= PULSE_MAX_COUNT &&
               read_be16(b + 2) && read_be16(b + 4);
    case FX_VIBRATO: {
        uint16_t start = read_be16(b + 1), end = read_be16(b + 3);
        uint16_t depth = read_be16(b + 7);
        return start > depth && end > depth && read_be16(b + 5) &&
               (uint32_t)start + depth <= 65535UL &&
               (uint32_t)end + depth <= 65535UL && read_be16(b + 9) >= 2;
    }
    case FX_ARPEGGIO:
        return read_be16(b + 1) && read_be16(b + 1) <= 32767 &&
               read_be16(b + 3) && b[7] <= 1;
    case FX_GATE:
        return read_be16(b + 1) && read_be16(b + 3) && read_be16(b + 5);
    case FX_PRESET:
        return b[1] && b[1] < PRESET_COUNT;
    default: return false;
    }
}

static inline bool command_valid(const uint8_t *b) {
    switch (b[0]) {
    case COMMAND_PLAY:
        return b[1] && !(b[3] & (uint8_t)~PLAY_ENQUEUE) &&
               valid_scale(b[4]) && valid_scale(b[5]) && effect_valid(b + 6);
    case COMMAND_STOP:
    case COMMAND_CLEAR_QUEUE:
        return b[1] != 0;
    case COMMAND_UPDATE:
        return b[1] && b[2] && b[3] && !(b[3] & (uint8_t)~7u) &&
               (!(b[3] & UPDATE_PITCH) || valid_scale(b[4])) &&
               (!(b[3] & UPDATE_SPEED) || valid_scale(b[5])) &&
               (!(b[3] & UPDATE_MODULATION) || read_be16(b + 8) >= 2);
    case COMMAND_ACK_EVENT: return true;
    default: return false;
    }
}

typedef struct {
    uint8_t clock_select, top, compare;
    bool toggle;
} tone_config_t;

static inline tone_config_t tone_config(uint16_t frequency) {
    tone_config_t config = {0, 0, 0, false};
    if (!frequency)
        return config;
    config.toggle = frequency == 1;
    uint32_t rate = (uint32_t)frequency * (config.toggle ? 2u : 1u);
    uint32_t divisor = 1;
    for (uint8_t select = 1; select <= 15; ++select, divisor <<= 1) {
        uint32_t denominator = rate * divisor;
        uint32_t period = (8000000UL + denominator / 2) / denominator;
        if (period >= 2 && period <= 256) {
            config.clock_select = select;
            config.top = (uint8_t)(period - 1);
            config.compare = config.toggle ? config.top : (uint8_t)(period / 2);
            return config;
        }
    }
    return config;
}
#endif
