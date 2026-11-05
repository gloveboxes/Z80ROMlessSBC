#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main attiny_firmware_main
#include "../flash_ATTiny85/target/main.c"
#undef main

static void start_write(void) {
    PINB = SDA_MASK; /* SCL low: START handshake may complete. */
    USI_START_vect();
    assert(start_pending);
    service_start();
    assert(!start_pending && receive_state == ADDRESS);
    USIDR = ATTINY85_I2C_ADDRESS << 1;
    USI_OVF_vect();
    assert(receive_state == ADDRESS_ACK && (DDRB & SDA_MASK));
    USI_OVF_vect();
    assert(receive_state == DATA && !(DDRB & SDA_MASK));
}

static void send_byte(uint8_t byte, bool final) {
    USIDR = byte;
    USI_OVF_vect();
    assert(receive_state == (final ? FINAL_ACK : DATA_ACK));
    assert(DDRB & SDA_MASK);
    USI_OVF_vect();
}

int main(void) {
    assert(command_length(1) == 5 && command_length(2) == 2);
    assert(command_length(3) == 2 && command_length(4) == 0);
    uint8_t endian[] = {0x12, 0x34};
    assert(read_be16(endian) == 0x1234);

    for (unsigned value = 0; value < 256; ++value) {
        unsigned on = 0;
        for (unsigned phase = 0; phase < 256; ++phase) {
            on += led_on((uint8_t)value, (uint8_t)phase);
        }
        assert(on == (value == 255 ? 256 : value));
    }
    assert(tone_config(0).clock_select == 0);
    for (uint32_t frequency = 1; frequency <= 65535; ++frequency) {
        tone_config_t config = tone_config((uint16_t)frequency);
        assert(config.clock_select >= 1 && config.clock_select <= 15);
        uint32_t divisor = 1UL << (config.clock_select - 1);
        double actual = 8000000.0 / (divisor * (config.top + 1u) *
                                    (config.toggle ? 2u : 1u));
        double error = actual / frequency - 1;
        assert(error > -0.01 && error < 0.01);
    }

    start_write();
    send_byte(COMMAND_TONE, false);
    send_byte(0x03, false);
    send_byte(0xe8, false);
    send_byte(0x01, false);
    send_byte(0xf4, true); /* 1000 Hz, 500 ms */
    command_t command;
    assert(dequeue(&command) && !dequeue(&(command_t){0}));
    execute(&command);
    assert(tone_remaining_ms == 500 && (TCCR1 & _BV(PWM1A)));
    for (unsigned i = 0; i < 15624; ++i) {
        TIM0_COMPA_vect();
    }
    assert(tone_remaining_ms == 1 && TCCR1);
    TIM0_COMPA_vect();
    assert(tone_remaining_ms == 0 && TCCR1 == 0 && !(PORTB & _BV(PB1)));

    execute(&(command_t){{COMMAND_TONE, 0, 1, 0xff, 0xff}});
    assert(tone_remaining_ms == 65535 && (TCCR1 & _BV(CTC1)));
    execute(&(command_t){{COMMAND_TONE, 0, 0, 0, 0}});
    assert(TCCR1 == 0);
    execute(&(command_t){{COMMAND_TONE, 1, 0, 0, 0}});
    assert(TCCR1 == 0);
    execute(&(command_t){{COMMAND_TONE, 1, 0, 0, 10}});
    execute(&(command_t){{COMMAND_TONE, 2, 0, 0, 20}});
    assert(tone_remaining_ms == 20); /* New tone replaces the current tone. */

    start_write();
    send_byte(COMMAND_RED, false); /* Truncated packet, then repeated START. */
    assert(!dequeue(&command));
    start_write();
    send_byte(COMMAND_BLUE, false);
    send_byte(255, true);
    assert(dequeue(&command));
    execute(&command);
    assert(blue == 255);

    start_write();
    USIDR = 0x99;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK) && !(USICR & _BV(USIOIE)));
    assert(!dequeue(&command));

    for (unsigned i = 0; i < QUEUE_SIZE - 1; ++i) {
        start_write();
        send_byte(COMMAND_RED, false);
        send_byte((uint8_t)i, true);
    }
    start_write();
    send_byte(COMMAND_RED, false);
    USIDR = 20;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK)); /* Full queue NACKs the final payload byte. */
    for (unsigned i = 0; i < QUEUE_SIZE - 1; ++i) {
        assert(dequeue(&command) && command.bytes[1] == i);
    }
    assert(!dequeue(&command));

    PINB = SCL_MASK; /* Master stalls after START: no ISR busy wait. */
    USI_START_vect();
    service_start();
    assert(start_pending);
    PINB = SCL_MASK | SDA_MASK; /* Cancel with STOP. */
    service_start();
    assert(!start_pending);

    start_write();
    receive_state = ADDRESS;
    USIDR = (ATTINY85_I2C_ADDRESS << 1) | 1u;
    USI_OVF_vect();
    assert(!(DDRB & SDA_MASK)); /* Read requests are not supported. */
    puts("ATtiny85 protocol, USI states, PWM and tone tests passed.");
    return 0;
}
