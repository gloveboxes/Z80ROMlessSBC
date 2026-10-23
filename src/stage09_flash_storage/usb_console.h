#ifndef Z80_USB_CONSOLE_H
#define Z80_USB_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Register bounded diagnostic output on core 0 before launching core 1. */
void z80_usb_console_prepare(void);
/* USB, its alarm pool and all endpoint operations belong to core 1. */
bool z80_usb_console_init(void);
void z80_usb_console_service(void);
size_t z80_usb_console_write(const uint8_t *data, size_t length);
uint32_t z80_usb_console_log_drops(void);

#endif
