#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/atomic.h>

#include "protocol.h"

#if F_CPU != 8000000UL
#error "Timer configuration requires an 8 MHz CPU clock"
#endif

#define SDA_MASK _BV(PB0)
#define SCL_MASK _BV(PB2)
#define LED_MASK (_BV(PB3) | _BV(PB4))
#define USI_IDLE (_BV(USISIE) | _BV(USIWM1) | _BV(USICS1))
#define USI_ACTIVE (USI_IDLE | _BV(USIOIE) | _BV(USIWM0))
#define USI_FLAGS (_BV(USISIF) | _BV(USIOIF) | _BV(USIPF) | _BV(USIDC))
#define QUEUE_SIZE 4u

typedef struct {
    uint8_t bytes[COMMAND_MAX_BYTES];
} command_t;

static volatile command_t queue[QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static uint8_t packet[COMMAND_MAX_BYTES];
static uint8_t packet_size;
static uint8_t expected_size;
static volatile uint8_t start_pending;
static volatile uint8_t red;
static volatile uint8_t blue;
static volatile uint16_t milliseconds;
static volatile uint16_t tick_us;

enum effect_mode { EFFECT_NONE, EFFECT_HOLD, EFFECT_SWEEP, EFFECT_SIREN,
                   EFFECT_SEQUENCE, EFFECT_FADE, EFFECT_BLINK, EFFECT_BREATHE,
                   EFFECT_SEGMENTS, EFFECT_PULSES, EFFECT_VIBRATO };

typedef struct {
    uint16_t frequency;
    uint16_t duration;
} note_t;

static struct {
    uint8_t mode, count, index;
    uint16_t from, to, rise, fall, remaining, repeats, frequency;
    uint16_t depth, vibrato_period, vibrato_phase;
    uint32_t phase, period;
    note_t notes[SEQUENCE_MAX_STEPS];
} sound;

typedef struct {
    uint8_t mode, from, to, idle;
    uint16_t rise, fall, remaining, repeats;
    uint32_t phase;
} led_effect_t;

static led_effect_t lights[2];
static uint16_t serviced_ms;

enum receive_state { ADDRESS, ADDRESS_ACK, DATA, DATA_ACK, FINAL_ACK };
static enum receive_state receive_state;

static void tone_stop(void) {
    TCCR1 = 0;
    PORTB &= (uint8_t)~_BV(PB1);
    sound.frequency = 0;
}

static void set_frequency(uint16_t frequency) {
    if (frequency == sound.frequency) {
        return;
    }
    tone_config_t config = tone_config(frequency);
    uint8_t control = config.clock_select |
        (config.toggle ? (_BV(CTC1) | _BV(COM1A0)) :
                         (_BV(PWM1A) | _BV(COM1A1)));
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (!config.clock_select) {
            tone_stop();
        } else if (TCCR1 == control) {
            if (OCR1C != config.top || OCR1A != config.compare) {
                /* OCR1C is not buffered. Pause without disconnecting OC1A;
                 * retain phase unless the new TOP would miss a match. */
                TCCR1 = control & 0xf0u;
                if (TCNT1 >= config.top) {
                    TCNT1 = 0;
                }
                OCR1C = config.top;
                OCR1A = config.compare;
                TCCR1 = control;
            }
        } else {
            tone_stop();
            TCNT1 = 0;
            OCR1C = config.top;
            OCR1A = config.compare;
            GTCCR = _BV(PSR1);
            TCCR1 = control;
        }
        sound.frequency = frequency;
    }
}

ISR(TIM0_COMPA_vect) {
    static uint8_t phase;
    uint8_t outputs = 0;
    if (led_on(red, phase)) {
        outputs |= _BV(PB3);
    }
    if (led_on(blue, phase)) {
        outputs |= _BV(PB4);
    }
    PORTB = (PORTB & (uint8_t)~LED_MASK) | outputs;
    ++phase;

    tick_us += 32;
    if (tick_us >= 1000) {
        tick_us -= 1000;
        ++milliseconds;
    }
}

static uint16_t interpolate(uint16_t from, uint16_t to,
                            uint32_t phase, uint16_t duration) {
    if (to >= from) {
        return from + (uint32_t)(to - from) * phase / duration;
    }
    return from - (uint32_t)(from - to) * phase / duration;
}

static bool expired(uint16_t *remaining, uint16_t elapsed) {
    if (!*remaining) {
        return false; /* Explicit indefinite duration. */
    }
    if (elapsed >= *remaining) {
        *remaining = 0;
        return true;
    }
    *remaining -= elapsed;
    return false;
}

static bool cycle_advance(uint32_t *phase, uint32_t period,
                          uint16_t *repeats, uint16_t elapsed) {
    *phase += elapsed;
    uint32_t cycles = *phase / period;
    *phase %= period;
    if (*repeats) {
        if (cycles >= *repeats) {
            return false;
        }
        *repeats -= cycles;
    }
    return true;
}

static uint16_t triangle(uint16_t from, uint16_t to, uint16_t rise,
                         uint16_t fall, uint32_t phase) {
    return phase < rise ? interpolate(from, to, phase, rise) :
                          interpolate(to, from, phase - rise, fall);
}

static void update_sound(uint16_t elapsed) {
    uint16_t frequency = sound.from;
    switch (sound.mode) {
    case EFFECT_NONE:
        return;
    case EFFECT_HOLD:
    case EFFECT_SWEEP:
    case EFFECT_SIREN:
    case EFFECT_VIBRATO:
        if (expired(&sound.remaining, elapsed)) {
            sound.mode = EFFECT_NONE;
            set_frequency(0);
            return;
        }
        if (sound.mode == EFFECT_SWEEP || sound.mode == EFFECT_VIBRATO) {
            sound.phase += elapsed;
            frequency = interpolate(sound.from, sound.to, sound.phase, sound.rise);
            if (sound.mode == EFFECT_VIBRATO) {
                sound.vibrato_phase = ((uint32_t)sound.vibrato_phase + elapsed) %
                                      sound.vibrato_period;
                uint16_t half = sound.vibrato_period / 2;
                /* Two interpolations avoid overflowing a 16-bit 2*depth. */
                int32_t offset = sound.vibrato_phase < half ?
                    (int32_t)interpolate(0, sound.depth, sound.vibrato_phase, half) * 2 :
                    (int32_t)interpolate(sound.depth, 0,
                        sound.vibrato_phase - half, sound.vibrato_period - half) * 2;
                frequency = (int32_t)frequency + offset - sound.depth;
            }
        } else if (sound.mode == EFFECT_SIREN) {
            sound.phase = (sound.phase + elapsed) %
                          ((uint32_t)sound.rise + sound.fall);
            frequency = triangle(sound.from, sound.to, sound.rise,
                                 sound.fall, sound.phase);
        }
        break;
    case EFFECT_SEQUENCE:
    case EFFECT_SEGMENTS: {
        if (!cycle_advance(&sound.phase, sound.period, &sound.repeats, elapsed)) {
            sound.mode = EFFECT_NONE;
            set_frequency(0);
            return;
        }
        uint32_t position = sound.phase;
        uint16_t from = sound.from;
        for (uint8_t i = 0; i < sound.count; ++i) {
            if (position < sound.notes[i].duration) {
                frequency = sound.mode == EFFECT_SEGMENTS ?
                    interpolate(from, sound.notes[i].frequency,
                                position, sound.notes[i].duration) :
                    sound.notes[i].frequency;
                break;
            }
            position -= sound.notes[i].duration;
            from = sound.notes[i].frequency;
        }
        break;
    }
    case EFFECT_PULSES:
        sound.phase += elapsed;
        while (sound.index < sound.count) {
            uint16_t gap = sound.fall;
            if (sound.count > 2 && sound.index < sound.count - 1u) {
                gap = interpolate(sound.fall, sound.to, sound.index, sound.count - 2u);
            }
            uint32_t length = (uint32_t)sound.rise + gap;
            if (sound.index == sound.count - 1u) {
                length = sound.rise; /* No trailing rest after the final beep. */
            }
            if (sound.phase < length) {
                frequency = sound.phase < sound.rise ? sound.from : 0;
                break;
            }
            sound.phase -= length;
            ++sound.index;
        }
        if (sound.index == sound.count) {
            sound.mode = EFFECT_NONE;
            frequency = 0;
        }
        break;
    default:
        return;
    }
    set_frequency(frequency);
}

static uint8_t update_light(led_effect_t *effect, uint16_t elapsed,
                             uint8_t current) {
    switch (effect->mode) {
    case EFFECT_FADE:
        if (expired(&effect->remaining, elapsed)) {
            effect->mode = EFFECT_NONE;
            return effect->to;
        }
        effect->phase += elapsed;
        return interpolate(effect->from, effect->to, effect->phase, effect->rise);
    case EFFECT_BLINK:
        if (!cycle_advance(&effect->phase, (uint32_t)effect->rise + effect->fall,
                           &effect->repeats, elapsed)) {
            effect->mode = EFFECT_NONE;
            return effect->idle;
        }
        return effect->phase < effect->rise ? effect->from : effect->to;
    case EFFECT_BREATHE:
        if (expired(&effect->remaining, elapsed)) {
            effect->mode = EFFECT_NONE;
            return effect->idle;
        }
        effect->phase = (effect->phase + elapsed) %
                        ((uint32_t)effect->rise + effect->fall);
        return triangle(effect->from, effect->to, effect->rise,
                        effect->fall, effect->phase);
    default:
        return current;
    }
}

static void render_effects(uint16_t elapsed) {
    update_sound(elapsed);
    uint8_t next_red = update_light(&lights[0], elapsed, red);
    uint8_t next_blue = update_light(&lights[1], elapsed, blue);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        red = next_red;
        blue = next_blue;
    }
}

static void service_effects(void) {
    uint16_t now;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        now = milliseconds;
    }
    uint16_t elapsed = now - serviced_ms;
    if (elapsed) {
        serviced_ms = now;
        render_effects(elapsed);
    }
}

static void usi_idle(void) {
    DDRB &= (uint8_t)~SDA_MASK;
    USIDR = 0xff;
    USICR = USI_IDLE;
    USISR = USI_FLAGS;
}

/* Leave the START flag set until the first falling SCL edge. Main finishes
 * the handshake, so a stalled master cannot block the duration/PWM ISR. */
ISR(USI_START_vect) {
    DDRB &= (uint8_t)~SDA_MASK;
    USICR = _BV(USIWM1) | _BV(USICS1);
    start_pending = 1;
}

static void receive_byte(void) {
    DDRB &= (uint8_t)~SDA_MASK;
    USIDR = 0xff;
    USISR = _BV(USIOIF);
}

static void acknowledge(enum receive_state next) {
    receive_state = next;
    USIDR = 0;
    DDRB |= SDA_MASK;
    USISR = _BV(USIOIF) | 14u;
}

static bool accept_byte(uint8_t byte) {
    if (packet_size == 0) {
        expected_size = command_length(byte);
    }
    if (expected_size == 0 || packet_size >= expected_size) {
        return false;
    }
    packet[packet_size++] = byte;
    if ((packet[0] == COMMAND_SEQUENCE || packet[0] == COMMAND_SEGMENTS) &&
        packet_size == 2) {
        uint8_t limit = packet[0] == COMMAND_SEQUENCE ?
                        SEQUENCE_MAX_STEPS : SEGMENT_MAX_STEPS;
        if (!byte || byte > limit) {
            return false;
        }
        expected_size = packet_length(packet);
    }
    if (packet_size == expected_size) {
        if (!command_valid(packet)) {
            return false;
        }
        uint8_t next = (queue_head + 1u) % QUEUE_SIZE;
        if (next == queue_tail) {
            return false;
        }
        for (uint8_t i = 0; i < expected_size; ++i) {
            queue[queue_head].bytes[i] = packet[i];
        }
        queue_head = next;
    }
    return true;
}

ISR(USI_OVF_vect) {
    switch (receive_state) {
    case ADDRESS:
        if (USIDR == (ATTINY85_I2C_ADDRESS << 1)) {
            acknowledge(ADDRESS_ACK);
        } else {
            usi_idle();
        }
        break;
    case ADDRESS_ACK:
    case DATA_ACK:
        receive_state = DATA;
        receive_byte();
        break;
    case DATA: {
        uint8_t byte = USIDR;
        if (!accept_byte(byte)) {
            usi_idle(); /* Releasing SDA sends NACK; wait for another START. */
        } else {
            acknowledge(packet_size == expected_size ? FINAL_ACK : DATA_ACK);
        }
        break;
    }
    case FINAL_ACK:
        usi_idle();
        break;
    }
}

static void service_start(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (start_pending) {
            if (!(PINB & SCL_MASK)) {
                packet_size = 0;
                expected_size = 0;
                receive_state = ADDRESS;
                USIDR = 0xff;
                USICR = USI_ACTIVE;
                start_pending = 0;
                USISR = USI_FLAGS;
            } else if (PINB & SDA_MASK) {
                start_pending = 0; /* START was cancelled by STOP. */
                usi_idle();
            }
        }
    }
}

static bool dequeue(command_t *command) {
    bool available = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (queue_tail != queue_head) {
            *command = queue[queue_tail];
            queue_tail = (queue_tail + 1u) % QUEUE_SIZE;
            available = true;
        }
    }
    return available;
}

static void execute(const command_t *command) {
    const uint8_t *bytes = command->bytes;
    if (bytes[0] == COMMAND_RED) {
        lights[0].mode = EFFECT_NONE;
        red = bytes[1];
    } else if (bytes[0] == COMMAND_BLUE) {
        lights[1].mode = EFFECT_NONE;
        blue = bytes[1];
    } else if (bytes[0] == COMMAND_TONE) {
        uint16_t frequency = read_be16(bytes + 1);
        uint16_t duration = read_be16(bytes + 3);
        sound.mode = frequency && duration ? EFFECT_HOLD : EFFECT_NONE;
        sound.from = frequency;
        sound.remaining = duration;
        set_frequency(sound.mode ? frequency : 0);
    } else if (bytes[0] == COMMAND_STOP) {
        if (bytes[1] & CHANNEL_SOUND) {
            sound.mode = EFFECT_NONE;
            set_frequency(0);
        }
        if (bytes[1] & CHANNEL_RED) {
            lights[0].mode = EFFECT_NONE;
            red = 0;
        }
        if (bytes[1] & CHANNEL_BLUE) {
            lights[1].mode = EFFECT_NONE;
            blue = 0;
        }
    } else if (bytes[0] == COMMAND_SWEEP || bytes[0] == COMMAND_SIREN ||
               bytes[0] == COMMAND_CUE) {
        sound.mode = bytes[0] == COMMAND_SIREN ? EFFECT_SIREN : EFFECT_SWEEP;
        sound.from = read_be16(bytes + 1);
        sound.to = read_be16(bytes + 3);
        sound.rise = read_be16(bytes + 5);
        sound.fall = bytes[0] == COMMAND_SIREN ? read_be16(bytes + 7) : 0;
        sound.remaining = bytes[0] == COMMAND_SIREN ?
                          read_be16(bytes + 9) : sound.rise;
        sound.phase = 0;
        if (bytes[0] == COMMAND_CUE) {
            for (uint8_t i = 0; i < 2; ++i) {
                lights[i] = (led_effect_t){
                    .mode = EFFECT_FADE, .from = bytes[7 + 2u * i],
                    .to = bytes[8 + 2u * i], .rise = sound.rise,
                    .remaining = sound.rise
                };
            }
        }
    } else if (bytes[0] == COMMAND_SEQUENCE || bytes[0] == COMMAND_SEGMENTS) {
        sound.mode = bytes[0] == COMMAND_SEQUENCE ? EFFECT_SEQUENCE : EFFECT_SEGMENTS;
        sound.count = bytes[1];
        sound.repeats = read_be16(bytes + 2);
        sound.phase = sound.period = 0;
        uint8_t offset = bytes[0] == COMMAND_SEQUENCE ? 4 : 6;
        sound.from = bytes[0] == COMMAND_SEGMENTS ? read_be16(bytes + 4) : 0;
        for (uint8_t i = 0; i < sound.count; ++i) {
            sound.notes[i].frequency = read_be16(bytes + offset + 4u * i);
            sound.notes[i].duration = read_be16(bytes + offset + 2 + 4u * i);
            sound.period += sound.notes[i].duration;
        }
    } else if (bytes[0] == COMMAND_PULSES) {
        sound.mode = EFFECT_PULSES;
        sound.count = bytes[1];
        sound.index = 0;
        sound.from = read_be16(bytes + 2);
        sound.rise = read_be16(bytes + 4);
        sound.fall = read_be16(bytes + 6);
        sound.to = read_be16(bytes + 8);
        sound.phase = 0;
    } else if (bytes[0] == COMMAND_VIBRATO) {
        sound.mode = EFFECT_VIBRATO;
        sound.from = read_be16(bytes + 1);
        sound.to = read_be16(bytes + 3);
        sound.rise = sound.remaining = read_be16(bytes + 5);
        sound.depth = read_be16(bytes + 7);
        sound.vibrato_period = read_be16(bytes + 9);
        sound.vibrato_phase = 0;
        sound.phase = 0;
    } else {
        led_effect_t effect = {0};
        uint8_t mask = bytes[1];
        if (bytes[0] == COMMAND_FADE) {
            effect.mode = EFFECT_FADE;
            effect.from = bytes[2];
            effect.to = bytes[3];
            effect.rise = effect.remaining = read_be16(bytes + 4);
        } else if (bytes[0] == COMMAND_BLINK) {
            effect.mode = EFFECT_BLINK;
            effect.from = bytes[2];
            effect.rise = read_be16(bytes + 3);
            effect.fall = read_be16(bytes + 5);
            effect.repeats = read_be16(bytes + 7);
        } else if (bytes[0] == COMMAND_BREATHE) {
            effect.mode = EFFECT_BREATHE;
            effect.from = bytes[2];
            effect.to = bytes[3];
            effect.rise = read_be16(bytes + 4);
            effect.fall = read_be16(bytes + 6);
            effect.remaining = read_be16(bytes + 8);
            effect.idle = bytes[10];
        } else if (bytes[0] == COMMAND_ALTERNATE) {
            effect.mode = EFFECT_BLINK;
            effect.from = bytes[1];
            effect.rise = read_be16(bytes + 2);
            effect.fall = read_be16(bytes + 4);
            effect.repeats = read_be16(bytes + 6);
            mask = CHANNEL_LEDS;
        }
        for (uint8_t i = 0; i < 2; ++i) {
            if (mask & (1u << i)) {
                lights[i] = effect;
                if (bytes[0] == COMMAND_ALTERNATE && i == 1) {
                    lights[i].from = 0;
                    lights[i].to = effect.from;
                }
            }
        }
    }
    render_effects(0);
}

int main(void) {
    MCUSR = 0;
    wdt_disable();
    clock_prescale_set(clock_div_1);
    PORTB = SDA_MASK | SCL_MASK;
    /* USI needs the SCL output enabled to stretch the clock (open drain). */
    DDRB = _BV(PB1) | SCL_MASK | LED_MASK;
    PLLCSR = 0; /* Timer1 uses the CPU clock, not the PLL. */
    GTCCR = 0;  /* Disconnect OC1B and both complementary PWM outputs. */
    tone_stop();
    usi_idle();

    /* 8 MHz / 8 / 32 = 31,250 ticks/s; LED PWM is nominally 122 Hz. */
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01);
    OCR0A = 31;
    TIMSK = _BV(OCIE0A);
    sei();

    for (;;) {
        service_start();
        service_effects();
        command_t command;
        if (dequeue(&command)) {
            execute(&command);
        }
    }
}
