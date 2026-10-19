#include "z80sbc/terminal.h"

#include "pico/time.h"

bool z80_terminal_network_poll(void);
void z80_terminal_network_poll_input(void);
void z80_terminal_network_poll_output(void);

static uint32_t pending_output;
static uint32_t pending_input;
static repeating_timer_t output_timer;
static repeating_timer_t input_timer;

static bool output_timer_callback(repeating_timer_t *timer) {
  (void)timer;
  __atomic_store_n(&pending_output, 1, __ATOMIC_RELEASE);
  return true;
}

static bool input_timer_callback(repeating_timer_t *timer) {
  (void)timer;
  __atomic_store_n(&pending_input, 1, __ATOMIC_RELEASE);
  return true;
}

bool z80_terminal_init(void) {
  z80_terminal_queue_init();
  __atomic_store_n(&pending_output, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&pending_input, 0, __ATOMIC_RELEASE);
  if (!add_repeating_timer_ms(-20, output_timer_callback, NULL, &output_timer))
    return false;
  if (!add_repeating_timer_ms(-10, input_timer_callback, NULL, &input_timer)) {
    cancel_repeating_timer(&output_timer);
    return false;
  }
  return true;
}

void z80_terminal_core1_service(void) {
  bool ready = z80_terminal_network_poll();
  if (ready &&
      __atomic_exchange_n(&pending_input, 0, __ATOMIC_ACQ_REL))
    z80_terminal_network_poll_input();
  if (ready &&
      __atomic_exchange_n(&pending_output, 0, __ATOMIC_ACQ_REL))
    z80_terminal_network_poll_output();
}
