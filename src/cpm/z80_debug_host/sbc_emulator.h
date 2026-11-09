#ifndef Z80SBC_SBC_EMULATOR_H
#define Z80SBC_SBC_EMULATOR_H

#include <stdint.h>

// C API of the project's single Z80 emulator, used by host tests. One machine
// exists per process: sbc_reset() reinitializes it.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SBC_REG_AF, SBC_REG_BC, SBC_REG_DE, SBC_REG_HL, SBC_REG_IX, SBC_REG_IY,
    SBC_REG_SP, SBC_REG_PC, SBC_REG_AF2, SBC_REG_BC2, SBC_REG_DE2, SBC_REG_HL2,
    SBC_REG_I, SBC_REG_R, SBC_REG_IFF1, SBC_REG_IFF2, SBC_REG_IM,
    SBC_REG_COUNT
} sbc_reg_t;

typedef uint8_t (*sbc_in_fn)(uint16_t address, void *context);
typedef void (*sbc_out_fn)(uint16_t address, uint8_t value, void *context);

// Zeroes registers, both banks and the halt flag; selects bank 0.
void sbc_reset(void);
uint8_t *sbc_memory(void);

// Callbacks observe every IN/OUT. Bank selection happens before the OUT callback.
void sbc_set_io(sbc_in_fn in, sbc_out_fn out, void *context);

// The hook is called after an instruction stores to a watched address, with the
// byte now in memory. Only stores with an explicit address are reported:
// LD (BC|DE|HL|nn|IX+d|IY+d),r/n/rp, INC/DEC (HL|IX+d|IY+d).
typedef void (*sbc_write_fn)(uint16_t address, uint8_t value, void *context);
void sbc_set_write_hook(sbc_write_fn write, void *context);
void sbc_watch(uint16_t address, uint32_t count, int watched);

uint16_t sbc_get_reg(sbc_reg_t reg);
void sbc_set_reg(sbc_reg_t reg, uint16_t value);

int sbc_halted(void);
void sbc_set_halted(int halted);

// Events returned by sbc_run (a bit mask).
#define SBC_EVENT_TICKS 1       // the tick budget was consumed
#define SBC_EVENT_HALT 2        // HALT executed (or a callback requested a stop)
#define SBC_EVENT_BREAKPOINT 4  // PC reached a breakpoint before executing it

// Execution stops with the PC at a breakpoint, except for the first
// instruction of a run so a stopped program can resume.
void sbc_clear_breakpoints(void);
int sbc_add_breakpoint(uint16_t address);
void sbc_remove_breakpoint(uint16_t address);

// Runs until max_ticks T-states elapse, HALT executes or a breakpoint is hit.
// The last instruction may overshoot the budget; *ticks_used receives the
// T-states actually consumed. Returns SBC_EVENT_* bits.
uint32_t sbc_run(uint32_t max_ticks, uint32_t *ticks_used);

int sbc_active_bank(void);
void sbc_select_bank(int bank);
uint8_t *sbc_bank_memory(int bank);

#ifdef __cplusplus
}
#endif

#endif
