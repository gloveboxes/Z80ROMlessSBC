#ifndef Z80_STATUS_REPORT_H
#define Z80_STATUS_REPORT_H

#include <stdbool.h>

#include "z80sbc/io_trap.h"

/* Core 0 supervisor state that the console reports read. The reports run on
 * core 0 and only format output; they never change this state. */
typedef struct {
  bool running;
  bool boot_attempted;
  bool image_verified;
  bool bios_announced;
  bool ccp_announced;
  bool reset_was_watchdog;
  z80_io_trap_reset_fault_t reset_fault;
  const char *boot_failure;
} core0_status_t;

/* Ctrl-] s. `requested` adds the one-shot detail (reset cause, network, OLED,
 * speaker, I/O latency); otherwise only the periodic summary is printed. */
void status_print(const core0_status_t *state, bool requested);
void status_print_help(const core0_status_t *state);
void status_print_pio(void);
const char *status_boot_error_name(z80_boot_error_t error);

#endif
