#ifndef ATTINY85_PROTOCOL_H
#define ATTINY85_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define ATTINY85_I2C_ADDRESS 0x42u
#define COMMAND_TONE 0x01u
#define COMMAND_RED 0x02u
#define COMMAND_BLUE 0x03u
#define COMMAND_SWEEP 0x04u
#define COMMAND_SIREN 0x05u
#define COMMAND_SEQUENCE 0x06u
#define COMMAND_FADE 0x07u
#define COMMAND_BLINK 0x08u
#define COMMAND_BREATHE 0x09u
#define COMMAND_ALTERNATE 0x0au
#define COMMAND_STOP 0x0bu
#define COMMAND_CUE 0x0cu
#define COMMAND_SEGMENTS 0x0du
#define COMMAND_PULSES 0x0eu
#define COMMAND_VIBRATO 0x0fu
#define SEGMENT_MAX_STEPS 7u
#define PULSE_MAX_COUNT 64u
#define CHANNEL_RED 1u
#define CHANNEL_BLUE 2u
#define CHANNEL_SOUND 4u
#define CHANNEL_LEDS (CHANNEL_RED | CHANNEL_BLUE)
#define CHANNEL_ALL (CHANNEL_LEDS | CHANNEL_SOUND)
#define SEQUENCE_MAX_STEPS 8u
#define COMMAND_MAX_BYTES (4u + 4u * SEQUENCE_MAX_STEPS)

static inline uint8_t command_length(uint8_t command) {
    switch (command) {
    case COMMAND_TONE: return 5;
    case COMMAND_RED:
    case COMMAND_BLUE: return 2;
    case COMMAND_SWEEP: return 7;
    case COMMAND_SIREN: return 11;
    case COMMAND_SEQUENCE: return 4; /* Header; count determines final size. */
    case COMMAND_FADE: return 6;
    case COMMAND_BLINK: return 9;
    case COMMAND_BREATHE: return 11;
    case COMMAND_ALTERNATE: return 8;
    case COMMAND_STOP: return 2;
    case COMMAND_CUE: return 11;
    case COMMAND_SEGMENTS: return 6;
    case COMMAND_PULSES: return 10;
    case COMMAND_VIBRATO: return 11;
    default: return 0;
    }
}

static inline uint8_t packet_length(const uint8_t *b) {
    if (b[0] == COMMAND_SEQUENCE) {
        return 4u + 4u * b[1];
    }
    if (b[0] == COMMAND_SEGMENTS) {
        return 6u + 4u * b[1];
    }
    return command_length(b[0]);
}

static inline uint16_t read_be16(const uint8_t *bytes) {
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static inline bool valid_led_mask(uint8_t mask) {
    return mask && !(mask & (uint8_t)~CHANNEL_LEDS);
}

static inline bool command_valid(const uint8_t *b) {
    switch (b[0]) {
    case COMMAND_TONE:
    case COMMAND_RED:
    case COMMAND_BLUE:
        return true;
    case COMMAND_SWEEP:
    case COMMAND_CUE:
        return read_be16(b + 1) && read_be16(b + 3) && read_be16(b + 5);
    case COMMAND_SIREN:
        return read_be16(b + 1) && read_be16(b + 3) &&
               read_be16(b + 5) && read_be16(b + 7);
    case COMMAND_SEQUENCE:
        if (!b[1] || b[1] > SEQUENCE_MAX_STEPS) {
            return false;
        }
        for (uint8_t i = 0; i < b[1]; ++i) {
            if (!read_be16(b + 6 + 4u * i)) {
                return false;
            }
        }
        return true;
    case COMMAND_FADE:
        return valid_led_mask(b[1]) && read_be16(b + 4);
    case COMMAND_BLINK:
        return valid_led_mask(b[1]) && read_be16(b + 3) && read_be16(b + 5);
    case COMMAND_BREATHE:
        return valid_led_mask(b[1]) && b[2] <= b[3] &&
               read_be16(b + 4) && read_be16(b + 6);
    case COMMAND_ALTERNATE:
        return read_be16(b + 2) && read_be16(b + 4);
    case COMMAND_STOP:
        return b[1] && !(b[1] & (uint8_t)~CHANNEL_ALL);
    case COMMAND_SEGMENTS:
        if (!b[1] || b[1] > SEGMENT_MAX_STEPS || !read_be16(b + 4)) {
            return false;
        }
        for (uint8_t i = 0; i < b[1]; ++i) {
            if (!read_be16(b + 6 + 4u * i) || !read_be16(b + 8 + 4u * i)) {
                return false;
            }
        }
        return true;
    case COMMAND_PULSES:
        return b[1] && b[1] <= PULSE_MAX_COUNT &&
               read_be16(b + 2) && read_be16(b + 4);
    case COMMAND_VIBRATO: {
        uint16_t start = read_be16(b + 1), end = read_be16(b + 3);
        uint16_t depth = read_be16(b + 7);
        return start > depth && end > depth && read_be16(b + 5) &&
               (uint32_t)start + depth <= 65535UL &&
               (uint32_t)end + depth <= 65535UL && read_be16(b + 9) >= 2;
    }
    default:
        return false;
    }
}

typedef struct {
    uint8_t clock_select;
    uint8_t top;
    uint8_t compare;
    bool toggle;
} tone_config_t;

static inline tone_config_t tone_config(uint16_t frequency) {
    tone_config_t config = {0, 0, 0, false};
    if (frequency == 0) {
        return config;
    }
    /* PWM cannot reach 1 Hz at 8 MHz; hardware compare-toggle can. */
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

static inline bool led_on(uint8_t brightness, uint8_t phase) {
    return brightness == 255 || phase < brightness;
}

#endif
