#ifndef Z80SBC_FULL_MEMORY_TEST_H
#define Z80SBC_FULL_MEMORY_TEST_H

#include <stdbool.h>

void z80_full_memory_test_start(void);
bool z80_full_memory_test_active(void);
void z80_full_memory_test_tick(void);
void z80_full_memory_test_abort(const char *reason);

#endif
