#ifndef Z80_STAGE09_ANSI_INPUT_H
#define Z80_STAGE09_ANSI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define ANSI_INPUT_ESC_GRACE_MS 30u

void ansi_input_reset(void);
/* A zero input polls the Escape timer; a zero result means no key is ready. */
uint8_t ansi_input_process(uint8_t ch, uint32_t now_ms);
/* Whether a lone Escape awaits its timer, so only then is a poll needed. */
bool ansi_input_escape_pending(void);

#endif
