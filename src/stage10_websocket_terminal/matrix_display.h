#ifndef Z80_MATRIX_DISPLAY_H
#define Z80_MATRIX_DISPLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void z80_matrix_core1_service(bool updates_allowed);
void z80_matrix_set_wifi_connected(bool connected);
void z80_matrix_print_status(void);

#ifdef __cplusplus
}
#endif
#endif
