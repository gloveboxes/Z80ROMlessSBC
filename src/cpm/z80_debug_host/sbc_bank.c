#include "sbc_bank.h"

#include <string.h>

static uint8_t parked[SBC_BANK_COUNT][SBC_BANK_SIZE];
static int active;
static int available = 1;

void sbc_bank_reset(void)
{
    memset(parked, 0, sizeof(parked));
    active = 0;
    available = 1;
}

int sbc_bank_active(void)
{
    return active;
}

void sbc_bank_select(int bank)
{
    bank &= SBC_BANK_COUNT - 1;
    if (bank == active)
        return;
    memcpy(parked[active], memory, SBC_BANK_SIZE);
    memcpy(memory, parked[bank], SBC_BANK_SIZE);
    active = bank;
}

uint8_t *sbc_bank_view(int bank)
{
    bank &= SBC_BANK_COUNT - 1;
    return bank == active ? memory : parked[bank];
}

int sbc_bank_port_matches(uint8_t port)
{
    return (port & 0x17) == 7;
}

void sbc_bank_set_available(int is_available)
{
    available = is_available != 0;
}

int sbc_bank_available(void)
{
    return available;
}

uint8_t sbc_bank_status(void)
{
    return available ? (uint8_t)(SBC_BANK_STATUS | active) : 0;
}
