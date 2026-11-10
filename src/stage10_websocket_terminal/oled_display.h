#ifndef Z80_OLED_DISPLAY_H
#define Z80_OLED_DISPLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Core 0 only: shares the I2C bus with the speaker bridge. */
void z80_oled_service(bool updates_allowed);
void z80_oled_print_status(void);
#ifdef __cplusplus
}
#endif

#endif
