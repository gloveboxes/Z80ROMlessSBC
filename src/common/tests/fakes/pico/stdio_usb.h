#ifndef TEST_PICO_STDIO_USB_H
#define TEST_PICO_STDIO_USB_H
#include <stdbool.h>
#define PICO_STDIO_USB_DEFAULT_CRLF 1
bool stdio_usb_init(void);
bool stdio_usb_connected(void);
#endif
