#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint TEST_OUTPUT_PINS[] = {
  PIN_CLK,
  PIN_RESET_N,
  PIN_BUSREQ_N,
  PIN_SRAM_CE_N,
  PIN_DATA_DIR,
  PIN_DATA_ENABLE,
  PIN_ADDR_ENABLE,
  PIN_SPI_SCK,
  PIN_SPI_MOSI,
  PIN_SPI_CS_N,
  PIN_SRAM_WE_N,
  PIN_SRAM_OE_N,
};

static const uint DATA_HEADER_PINS[] = {14, 15, 16, 17, 19, 20, 21, 22};
static int data_step = -1;
static bool reset_high = false;

static uint print_data_levels(void) {
  uint high_mask = 0;
  printf("Data GPIO levels:");
  for (uint bit = 0; bit < 8; ++bit) {
    bool level = gpio_get(PIN_DATA_0 + bit);
    high_mask |= (uint)level << bit;
    printf(" D%u/GP%u=%u", bit, PIN_DATA_0 + bit, (unsigned)level);
  }
  printf("\n");
  return high_mask;
}

static void restore_safe_levels(void) {
  z80_safe_startup();
  data_step = -1;
  reset_high = false;
}

static void toggle_reset_output(void) {
  reset_high = !reset_high;
  gpio_put(PIN_RESET_N, reset_high);
  printf("RESET# GP3 commanded %s: Pico header pin 5, Z80 socket pin 26, GAL socket pin 1\n",
         reset_high ? "HIGH" : "LOW");
    printf("Expected voltage at all three contacts: %s (measure relative to common GND)\n",
      reset_high ? "3.20-3.40 V" : "near 0 V");
    printf("RESET# must never exceed the Pico 3.3 V rail; no 5 V pull-up permitted\n");
  printf("r: toggle RESET#, x: restore safe levels (Phase 1 only); measure the voltage\n");
}

static void step_data_output(void) {
  if (data_step < 0) {
    restore_safe_levels();
    if (print_data_levels() != 0) {
      printf("NOT STARTED: data inputs must all be LOW; check RN3 and wiring\n");
      return;
    }
    for (uint bit = 0; bit < 8; ++bit)
      output_with_initial_level(PIN_DATA_0 + bit, false);
  } else {
    gpio_put(PIN_DATA_0 + (uint)data_step, false);
  }

  ++data_step;
  if (data_step == 8) {
    restore_safe_levels();
    printf("DONE: data steps finished; safe levels restored, GP10-GP17 inputs\n");
    return;
  }

  uint bit = (uint)data_step;
  gpio_put(PIN_DATA_0 + bit, true);
  printf("D%u HIGH: GP%u, Pico header pin %u, AHCT245/LVC245 A%u pin %u\n",
         bit, PIN_DATA_0 + bit, DATA_HEADER_PINS[bit], bit + 1, bit + 2);
  printf("Other data pins LOW; d: next, x: restore safe levels (Phase 1 only)\n");
}

static void print_status(void) {
  printf("BUSACK#=%u IORQ#=%u RD#=%u WR#=%u MISO=%u\n",
         gpio_get(PIN_BUSACK_N), gpio_get(PIN_IORQ_N),
         gpio_get(PIN_RD_N), gpio_get(PIN_WR_N),
         gpio_get(PIN_SPI_MISO));
  print_data_levels();
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();

  printf("\nZ80 ROMless SBC - Stage 1 supervisor\n");
  printf("r: toggle RESET# HIGH/LOW (Phase 1 hardware only)\n");
  printf("w: walking output test (Phase 1 hardware only)\n");
  printf("d: step D0-D7, hold one HIGH (Phase 1 hardware only)\n");
  printf("x: restore safe levels and return data GPIOs to inputs\n");
  printf("s: sample input status\n");

  while (true) {
    if (!stdio_usb_connected()) {
      if (data_step >= 0 || reset_high)
        restore_safe_levels();
      tight_loop_contents();
      continue;
    }

    int command = getchar_timeout_us(0);
    if (command == 'r') {
      toggle_reset_output();
    } else if (command == 'w') {
      restore_safe_levels();
      printf("walking %u outputs\n",
             (unsigned)(sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0])));
      z80_walking_output_test(TEST_OUTPUT_PINS,
                              sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0]),
                              250);
      printf("PASS: safe levels restored\n");
    } else if (command == 'd') {
      step_data_output();
    } else if (command == 'x') {
      restore_safe_levels();
      printf("DONE: safe levels restored, GP10-GP17 inputs\n");
    } else if (command == 's') {
      print_status();
    }
    tight_loop_contents();
  }
}