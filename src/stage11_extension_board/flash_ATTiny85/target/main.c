#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include <string.h>

#include "protocol.h"
#include "presets.h"

#if F_CPU != 8000000UL
#error "Timer configuration requires an 8 MHz CPU clock"
#endif

#define SDA_MASK _BV(PB0)
#define SCL_MASK _BV(PB2)
#define USI_IDLE (_BV(USISIE) | _BV(USIWM1) | _BV(USICS1))
#define USI_ACTIVE (USI_IDLE | _BV(USIOIE) | _BV(USIWM0))
#define USI_FLAGS (_BV(USISIF) | _BV(USIOIF) | _BV(USIPF) | _BV(USIDC))

typedef struct { uint8_t bytes[COMMAND_MAX_BYTES]; } command_t;
typedef struct { uint16_t frequency, duration; } note_t;
static uint8_t packet[COMMAND_MAX_BYTES], packet_size, expected_size;
static volatile uint8_t command_pending, start_pending;
static volatile uint16_t milliseconds;
static uint16_t serviced_ms;
static command_t playback[PLAYBACK_QUEUE_SIZE];
static uint8_t playback_count;
static struct {
    uint8_t mode, id, priority, preset, pitch, speed, fraction, count, index;
    uint16_t from, to, rise, fall, remaining, repeats, frequency;
    uint16_t depth, vibrato_period, vibrato_phase;
    uint32_t phase, period;
    note_t notes[SEQUENCE_MAX_STEPS];
} sound;
static struct { uint16_t sequence; uint8_t id, type; } events[EVENT_CAPACITY];
static uint8_t event_head, event_count, event_overflow;
static uint16_t event_sequence, completed_count, rejected_count;
static uint8_t request_id, request_result;
static uint8_t published[STATUS_BYTES], transmit[STATUS_BYTES], transmit_position;

enum receive_state { ADDRESS, ADDRESS_ACK, DATA, DATA_ACK, FINAL_ACK,
                     TRANSMIT, TRANSMIT_ACK };
static enum receive_state receive_state;
static bool reading;

static void tone_stop(void) {
    TCCR1 = 0;
    PORTB &= (uint8_t)~_BV(PB1);
    sound.frequency = 0;
}

static void set_frequency(uint16_t frequency) {
    if (frequency == sound.frequency)
        return;
    tone_config_t config = tone_config(frequency);
    uint8_t control = config.clock_select |
        (config.toggle ? (_BV(CTC1) | _BV(COM1A0)) : (_BV(PWM1A) | _BV(COM1A1)));
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (!frequency) {
            tone_stop();
        } else if (TCCR1 == control) {
            if (OCR1C != config.top || OCR1A != config.compare) {
                /* OCR1C is not buffered; avoid missing a lowered TOP. */
                TCCR1 = control & 0xf0u;
                if (TCNT1 >= config.top)
                    TCNT1 = 0;
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

ISR(TIM0_COMPA_vect) { ++milliseconds; }

static uint16_t interpolate(uint16_t from, uint16_t to, uint32_t phase,
                            uint16_t duration) {
    return to >= from ? from + (uint32_t)(to - from) * phase / duration :
                        from - (uint32_t)(from - to) * phase / duration;
}

static void event(uint8_t id, uint8_t type) {
    if (event_count == EVENT_CAPACITY) {
        event_overflow = 1;
        return;
    }
    uint8_t slot = (event_head + event_count) % EVENT_CAPACITY;
    if (++event_sequence == 0)
        ++event_sequence;
    events[slot].sequence = event_sequence;
    events[slot].id = id;
    events[slot].type = type;
    ++event_count;
}

static void publish_status(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        published[S_VERSION] = PROTOCOL_VERSION;
        published[S_FLAGS] = (sound.mode ? STATUS_PLAYING : 0) |
            (command_pending ? STATUS_COMMAND_PENDING : 0) |
            (event_overflow ? STATUS_EVENT_OVERFLOW : 0);
        published[S_ACTIVE_ID] = sound.mode ? sound.id : 0;
        published[S_PRIORITY] = sound.mode ? sound.priority : 0;
        published[S_EFFECT] = sound.mode;
        published[S_PRESET] = sound.mode ? sound.preset : 0;
        published[S_QUEUED] = playback_count;
        published[S_FREQUENCY_HI] = sound.frequency >> 8;
        published[S_FREQUENCY_LO] = sound.frequency;
        published[S_REQUEST_ID] = request_id;
        published[S_RESULT] = request_result;
        uint16_t sequence = event_count ? events[event_head].sequence : 0;
        published[S_EVENT_SEQ_HI] = sequence >> 8;
        published[S_EVENT_SEQ_LO] = sequence;
        published[S_EVENT_ID] = event_count ? events[event_head].id : 0;
        published[S_EVENT_TYPE] = event_count ? events[event_head].type : 0;
        published[S_COMPLETED_HI] = completed_count >> 8;
        published[S_COMPLETED_LO] = completed_count;
        published[S_REJECTED_HI] = rejected_count >> 8;
        published[S_REJECTED_LO] = rejected_count;
    }
}

static void finish_sound(void) {
    event(sound.id, EVENT_COMPLETED);
    ++completed_count;
    sound.mode = 0;
    set_frequency(0);
}

static bool expired(uint16_t elapsed) {
    if (!sound.remaining)
        return false;
    if (elapsed < sound.remaining) {
        sound.remaining -= elapsed;
        return false;
    }
    finish_sound();
    return true;
}

static uint16_t triangle(uint16_t from, uint16_t to, uint16_t rise,
                         uint16_t fall, uint32_t phase) {
    return phase < rise ? interpolate(from, to, phase, rise) :
                          interpolate(to, from, phase - rise, fall);
}

static void update_sound(uint16_t elapsed) {
    if (!sound.mode)
        return;
    uint32_t scaled = (uint32_t)elapsed * sound.speed + sound.fraction;
    uint32_t ticks = scaled / 100;
    sound.fraction = scaled % 100;
    uint16_t dt = (uint16_t)ticks;
    uint16_t frequency = sound.from;
    switch (sound.mode) {
    case FX_TONE:
    case FX_SWEEP:
    case FX_SIREN:
    case FX_VIBRATO:
    case FX_GATE:
    case FX_ARPEGGIO:
        if (expired(dt))
            return;
        if (sound.mode == FX_SWEEP || sound.mode == FX_VIBRATO) {
            sound.phase += dt;
            frequency = interpolate(sound.from, sound.to, sound.phase, sound.rise);
            if (sound.mode == FX_VIBRATO) {
                sound.vibrato_phase = ((uint32_t)sound.vibrato_phase + dt) % sound.vibrato_period;
                uint16_t half = sound.vibrato_period / 2;
                int32_t offset = sound.vibrato_phase < half ?
                    (int32_t)interpolate(0, sound.depth, sound.vibrato_phase, half) * 2 :
                    (int32_t)interpolate(sound.depth, 0, sound.vibrato_phase - half,
                                         sound.vibrato_period - half) * 2;
                frequency = (int32_t)frequency + offset - sound.depth;
            }
        } else if (sound.mode == FX_SIREN || sound.mode == FX_GATE) {
            sound.phase = (sound.phase + dt) % ((uint32_t)sound.rise + sound.fall);
            frequency = sound.mode == FX_GATE ?
                (sound.phase < sound.rise ? sound.from : 0) :
                triangle(sound.from, sound.to, sound.rise, sound.fall, sound.phase);
        } else if (sound.mode == FX_ARPEGGIO) {
            sound.phase = (sound.phase + dt) % ((uint32_t)sound.rise * 4);
            uint8_t step = sound.phase / sound.rise;
            uint16_t ratio = step == 0 ? 256 : step == 1 ?
                (sound.index ? 304 : 323) : step == 2 ? 384 : 512;
            frequency = (uint32_t)sound.from * ratio / 256;
        }
        break;
    case FX_SEQUENCE:
    case FX_SEGMENTS: {
        sound.phase += dt;
        uint32_t cycles = sound.phase / sound.period;
        sound.phase %= sound.period;
        if (sound.repeats) {
            if (cycles >= sound.repeats) {
                finish_sound();
                return;
            }
            sound.repeats -= cycles;
        }
        uint32_t position = sound.phase;
        uint16_t from = sound.from;
        for (uint8_t i = 0; i < sound.count; ++i) {
            if (position < sound.notes[i].duration) {
                frequency = sound.mode == FX_SEGMENTS ?
                    interpolate(from, sound.notes[i].frequency, position, sound.notes[i].duration) :
                    sound.notes[i].frequency;
                break;
            }
            position -= sound.notes[i].duration;
            from = sound.notes[i].frequency;
        }
        break;
    }
    case FX_PULSES:
        sound.phase += dt;
        while (sound.index < sound.count) {
            uint16_t gap = sound.fall;
            if (sound.count > 2 && sound.index < sound.count - 1u)
                gap = interpolate(sound.fall, sound.to, sound.index, sound.count - 2u);
            uint32_t length = sound.rise;
            if (sound.index != sound.count - 1u)
                length += gap;
            if (sound.phase < length) {
                frequency = sound.phase < sound.rise ? sound.from : 0;
                break;
            }
            sound.phase -= length;
            ++sound.index;
        }
        if (sound.index == sound.count) {
            finish_sound();
            return;
        }
        break;
    }
    uint32_t pitch = (uint32_t)frequency * sound.pitch / 100;
    if (frequency && !pitch)
        pitch = 1;
    set_frequency(pitch > 65535 ? 65535 : (uint16_t)pitch);
}

static void start_sound(const uint8_t *b) {
    sound.id = b[1];
    sound.priority = b[2];
    sound.pitch = b[4];
    sound.speed = b[5];
    sound.fraction = 0;
    sound.preset = 0;
    const uint8_t *fx = b + 6;
    uint8_t preset_buffer[36];
    if (fx[0] == FX_PRESET) {
        sound.preset = fx[1];
        memcpy_P(preset_buffer, presets[fx[1] - 1], sizeof(preset_buffer));
        fx = preset_buffer;
    }
    sound.mode = fx[0];
    sound.phase = sound.period = 0;
    sound.remaining = 0;
    if (fx[0] == FX_SEQUENCE || fx[0] == FX_SEGMENTS) {
        sound.count = fx[1];
        sound.repeats = read_be16(fx + 2);
        uint8_t offset = fx[0] == FX_SEQUENCE ? 4 : 6;
        sound.from = fx[0] == FX_SEGMENTS ? read_be16(fx + 4) : 0;
        for (uint8_t i = 0; i < sound.count; ++i) {
            sound.notes[i].frequency = read_be16(fx + offset + 4u * i);
            sound.notes[i].duration = read_be16(fx + offset + 2 + 4u * i);
            sound.period += sound.notes[i].duration;
        }
    } else if (fx[0] == FX_PULSES) {
        sound.count = fx[1];
        sound.index = 0;
        sound.from = read_be16(fx + 2);
        sound.rise = read_be16(fx + 4);
        sound.fall = read_be16(fx + 6);
        sound.to = read_be16(fx + 8);
    } else {
        sound.from = read_be16(fx + 1);
        if (fx[0] == FX_TONE) {
            sound.remaining = read_be16(fx + 3);
        } else if (fx[0] == FX_GATE || fx[0] == FX_ARPEGGIO) {
            sound.rise = read_be16(fx + 3);
            if (fx[0] == FX_GATE) {
                sound.fall = read_be16(fx + 5);
                sound.remaining = read_be16(fx + 7);
            } else {
                sound.remaining = read_be16(fx + 5);
                sound.index = fx[7];
            }
        } else {
            sound.to = read_be16(fx + 3);
            sound.rise = read_be16(fx + 5);
            sound.remaining = sound.rise;
            if (fx[0] == FX_SIREN) {
                sound.fall = read_be16(fx + 7);
                sound.remaining = read_be16(fx + 9);
            } else if (fx[0] == FX_VIBRATO) {
                sound.depth = read_be16(fx + 7);
                sound.vibrato_period = read_be16(fx + 9);
                sound.vibrato_phase = 0;
            }
        }
    }
    event(sound.id, EVENT_STARTED);
    update_sound(0);
}

static void start_queued(void) {
    if (!sound.mode && playback_count) {
        start_sound(playback[0].bytes);
        --playback_count;
        if (playback_count)
            playback[0] = playback[1];
    }
}

static void reject(uint8_t result) {
    request_result = result;
    ++rejected_count;
    event(request_id, EVENT_REJECTED);
}

static void clear_queue(void) {
    for (uint8_t i = 0; i < playback_count; ++i)
        event(playback[i].bytes[1], EVENT_CANCELLED);
    playback_count = 0;
}

static void execute(const uint8_t *b) {
    if (b[0] == COMMAND_ACK_EVENT) {
        if (event_count && events[event_head].sequence == read_be16(b + 1)) {
            event_head = (event_head + 1u) % EVENT_CAPACITY;
            --event_count;
        }
        if (!event_count)
            event_overflow = 0;
        return;
    }
    request_id = b[1];
    if (b[0] == COMMAND_PLAY) {
        if ((b[3] & PLAY_ENQUEUE) && sound.mode) {
            if (playback_count == PLAYBACK_QUEUE_SIZE) {
                reject(RESULT_QUEUE_FULL);
            } else {
                memcpy(playback[playback_count++].bytes, b, packet_length(b));
                request_result = RESULT_QUEUED;
                event(b[1], EVENT_QUEUED);
            }
        } else if (sound.mode && b[2] < sound.priority) {
            reject(RESULT_PRIORITY);
        } else {
            if (sound.mode)
                event(sound.id, EVENT_INTERRUPTED);
            start_sound(b);
            request_result = RESULT_ACCEPTED;
        }
    } else if (b[0] == COMMAND_STOP) {
        if (sound.mode)
            event(sound.id, EVENT_STOPPED);
        sound.mode = 0;
        set_frequency(0);
        clear_queue();
        request_result = RESULT_STOPPED;
    } else if (b[0] == COMMAND_CLEAR_QUEUE) {
        clear_queue();
        request_result = RESULT_CLEARED;
    } else if (b[0] == COMMAND_UPDATE) {
        if (!sound.mode || sound.id != b[2]) {
            reject(RESULT_NO_MATCH);
            return;
        }
        uint16_t depth = read_be16(b + 6);
        if ((b[3] & UPDATE_MODULATION) &&
            (sound.mode != FX_VIBRATO || sound.from <= depth || sound.to <= depth ||
             (uint32_t)sound.from + depth > 65535 ||
             (uint32_t)sound.to + depth > 65535)) {
            reject(RESULT_INVALID);
            return;
        }
        if (b[3] & UPDATE_PITCH)
            sound.pitch = b[4];
        if (b[3] & UPDATE_SPEED)
            sound.speed = b[5];
        if (b[3] & UPDATE_MODULATION) {
            uint16_t period = read_be16(b + 8);
            sound.vibrato_phase = (uint32_t)sound.vibrato_phase * period / sound.vibrato_period;
            sound.vibrato_period = period;
            sound.depth = depth;
        }
        update_sound(0);
        event(sound.id, EVENT_UPDATED);
        request_result = RESULT_UPDATED;
    }
}

static void service_effects(void) {
    uint16_t now;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { now = milliseconds; }
    uint16_t elapsed = now - serviced_ms;
    serviced_ms = now;
    /* Bound scaled time without blocking an ISR or losing elapsed ticks. */
    while (elapsed > 30000) {
        update_sound(30000);
        elapsed -= 30000;
    }
    if (elapsed)
        update_sound(elapsed);
    start_queued();
}

static void usi_idle(void) {
    DDRB &= (uint8_t)~SDA_MASK;
    USIDR = 0xff;
    USICR = USI_IDLE;
    USISR = USI_FLAGS;
}

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
    if (command_pending || packet_size >= COMMAND_MAX_BYTES)
        return false;
    packet[packet_size++] = byte;
    if (packet_size == 1) {
        switch (byte) {
        case COMMAND_PLAY: expected_size = 8; break;
        case COMMAND_STOP:
        case COMMAND_CLEAR_QUEUE: expected_size = 2; break;
        case COMMAND_UPDATE: expected_size = 10; break;
        case COMMAND_ACK_EVENT: expected_size = 3; break;
        default: return false;
        }
    }
    if (packet[0] == COMMAND_PLAY && packet_size == 8) {
        uint8_t *fx = packet + 6;
        if ((fx[0] == FX_SEQUENCE && (!fx[1] || fx[1] > SEQUENCE_MAX_STEPS)) ||
            (fx[0] == FX_SEGMENTS && (!fx[1] || fx[1] > SEGMENT_MAX_STEPS)))
            return false;
        expected_size = packet_length(packet);
        if (!expected_size || expected_size > COMMAND_MAX_BYTES)
            return false;
    }
    if (packet_size == expected_size) {
        if (!command_valid(packet))
            return false;
        __atomic_signal_fence(__ATOMIC_RELEASE);
        command_pending = 1;
    }
    return true;
}

static void transmit_byte(void) {
    USIDR = transmit_position < STATUS_BYTES ? transmit[transmit_position++] : 0xff;
    DDRB |= SDA_MASK;
    receive_state = TRANSMIT;
    USISR = _BV(USIOIF);
}

ISR(USI_OVF_vect) {
    switch (receive_state) {
    case ADDRESS:
        if ((USIDR >> 1) != ATTINY85_I2C_ADDRESS) {
            usi_idle();
            break;
        }
        reading = USIDR & 1u;
        if (reading) {
            memcpy(transmit, published, STATUS_BYTES);
            transmit[S_FLAGS] = (transmit[S_FLAGS] & (uint8_t)~STATUS_COMMAND_PENDING) |
                (command_pending ? STATUS_COMMAND_PENDING : 0);
            transmit_position = 0;
        }
        acknowledge(ADDRESS_ACK);
        break;
    case ADDRESS_ACK:
        if (reading) {
            transmit_byte();
            break;
        }
        receive_state = DATA;
        receive_byte();
        break;
    case DATA_ACK:
        receive_state = DATA;
        receive_byte();
        break;
    case DATA:
        if (!accept_byte(USIDR))
            usi_idle();
        else
            acknowledge(command_pending ? FINAL_ACK : DATA_ACK);
        break;
    case FINAL_ACK:
        usi_idle();
        break;
    case TRANSMIT:
        DDRB &= (uint8_t)~SDA_MASK;
        USIDR = 0xff;
        receive_state = TRANSMIT_ACK;
        USISR = _BV(USIOIF) | 14u;
        break;
    case TRANSMIT_ACK:
        if (USIDR & 1u)
            usi_idle();
        else
            transmit_byte();
        break;
    }
}

static void service_start(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (start_pending) {
            if (!(PINB & SCL_MASK)) {
                packet_size = expected_size = 0;
                receive_state = ADDRESS;
                USIDR = 0xff;
                USICR = USI_ACTIVE;
                start_pending = 0;
                USISR = USI_FLAGS;
            } else if (PINB & SDA_MASK) {
                start_pending = 0;
                usi_idle();
            }
        }
    }
}

static void service_command(void) {
    if (command_pending) {
        __atomic_signal_fence(__ATOMIC_ACQUIRE);
        execute(packet);
        __atomic_signal_fence(__ATOMIC_RELEASE);
        command_pending = 0;
    }
    publish_status();
}

int main(void) {
    MCUSR = 0;
    wdt_disable();
    clock_prescale_set(clock_div_1);
    PORTB = SDA_MASK | SCL_MASK;
    DDRB = _BV(PB1) | SCL_MASK;
    PLLCSR = GTCCR = 0;
    tone_stop();
    usi_idle();
    publish_status();
    /* 8 MHz / 64 / 125 = 1000 Hz; Timer1 owns the sound waveform. */
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01) | _BV(CS00);
    OCR0A = 124;
    TIMSK = _BV(OCIE0A);
    sei();
    for (;;) {
        service_start();
        service_effects();
        service_command();
    }
}
