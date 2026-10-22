#ifndef TEST_PICO_STDIO_DRIVER_H
#define TEST_PICO_STDIO_DRIVER_H
#include <stdbool.h>
typedef struct stdio_driver {
  void (*out_chars)(const char *, int);
  void (*out_flush)(void);
  int (*in_chars)(char *, int);
} stdio_driver_t;
extern stdio_driver_t stdio_usb;
void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled);
#endif
