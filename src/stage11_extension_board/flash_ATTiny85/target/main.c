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
static volatile uint16_t tone_remaining_ms;
static volatile uint16_t tick_us;

enum receive_state { ADDRESS, ADDRESS_ACK, DATA, DATA_ACK, FINAL_ACK };
static enum receive_state receive_state;

static void tone_stop(void) {
    TCCR1 = 0;
    PORTB &= (uint8_t)~_BV(PB1);
    tone_remaining_ms = 0;
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
        if (tone_remaining_ms && --tone_remaining_ms == 0) {
            tone_stop();
        }
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
    if (packet_size == expected_size) {
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
        red = bytes[1];
    } else if (bytes[0] == COMMAND_BLUE) {
        blue = bytes[1];
    } else if (bytes[0] == COMMAND_TONE) {
        uint16_t frequency = read_be16(bytes + 1);
        uint16_t duration = read_be16(bytes + 3);
        tone_config_t config = tone_config(frequency);
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            tone_stop();
            if (config.clock_select && duration) {
                TCNT1 = 0;
                OCR1C = config.top;
                OCR1A = config.compare;
                GTCCR = _BV(PSR1);
                tone_remaining_ms = duration;
                tick_us = 0;
                TCCR1 = config.clock_select |
                    (config.toggle ? (_BV(CTC1) | _BV(COM1A0)) :
                                     (_BV(PWM1A) | _BV(COM1A1)));
            }
        }
    }
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
        command_t command;
        if (dequeue(&command)) {
            execute(&command);
        }
    }
}
