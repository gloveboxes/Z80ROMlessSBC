#ifndef Z80_WEB_TERMINAL_H
#define Z80_WEB_TERMINAL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void z80_web_terminal_poll(bool online, uint32_t generation);
#ifdef __cplusplus
}
#endif
#endif
