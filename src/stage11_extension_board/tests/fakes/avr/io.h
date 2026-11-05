#ifndef TEST_AVR_IO_H
#define TEST_AVR_IO_H
#include <stdint.h>
#define F_CPU 8000000UL
#define _BV(bit) (1u << (bit))
enum { PB0, PB1, PB2, PB3, PB4 };
enum { USICNT0, USICNT1, USICNT2, USICNT3, USIDC, USIPF, USIOIF, USISIF };
enum { USITC, USICLK, USICS0, USICS1, USIWM0, USIWM1, USIOIE, USISIE };
enum { CS10, CS11, CS12, CS13, COM1A0, COM1A1, PWM1A, CTC1 };
enum { WGM00, WGM01 };
enum { CS00, CS01 };
enum { OCIE0A = 4, PSR1 = 1 };
static uint8_t TCCR1, PORTB, DDRB, PINB, USIDR, USICR, USISR;
static uint8_t TCNT1, OCR1C, OCR1A, GTCCR, MCUSR, PLLCSR;
static uint8_t TCCR0A, TCCR0B, OCR0A, TIMSK;
#endif
