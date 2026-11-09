#ifndef Z80SBC_SBC_BANK_H
#define Z80SBC_SBC_BANK_H

#include <stdint.h>

// Two-bank SRAM model of the board's A16 line. memory[] is always the active
// bank; the inactive bank is parked in storage. Any OUT whose port satisfies
// (port & 0x17) == 7 selects the bank from bit 0 of the value. This models
// software-visible bank isolation only, not bus timing.

#ifdef __cplusplus
extern "C" {
#endif

#define SBC_BANK_COUNT 2
#define SBC_BANK_SIZE 65536
// IN from the bank port returns this value ORed with the active bank while
// bank 1 is available, and 0 otherwise.
#define SBC_BANK_STATUS 0xB4

extern uint8_t memory[SBC_BANK_SIZE];

void sbc_bank_reset(void);
int sbc_bank_active(void);
void sbc_bank_select(int bank);
uint8_t *sbc_bank_view(int bank);
int sbc_bank_port_matches(uint8_t port);
void sbc_bank_set_available(int available);
int sbc_bank_available(void);
uint8_t sbc_bank_status(void);

#ifdef __cplusplus
}
#endif

#endif
