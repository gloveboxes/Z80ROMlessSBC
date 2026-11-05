#ifndef ATTINY85_PROTOCOL_H
#define ATTINY85_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define ATTINY85_I2C_ADDRESS 0x42u
#define COMMAND_TONE 0x01u
#define COMMAND_RED 0x02u
#define COMMAND_BLUE 0x03u
#define COMMAND_MAX_BYTES 5u

static inline uint8_t command_length(uint8_t command) {
    switch (command) {
    case COMMAND_TONE: return 5;
    case COMMAND_RED:
    case COMMAND_BLUE: return 2;
    default: return 0;
    }
}

static inline uint16_t read_be16(const uint8_t *bytes) {
    return ((uint16_t)bytes[0] << 8) | bytes[1];
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
