#ifndef TEST_CYW43_ARCH_H
#define TEST_CYW43_ARCH_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cyw43.h"
#define CYW43_WL_GPIO_LED_PIN 0
void cyw43_arch_lwip_begin(void);
void cyw43_arch_lwip_end(void);
int cyw43_arch_init(void);
void cyw43_arch_deinit(void);
void cyw43_arch_enable_sta_mode(void);
void cyw43_arch_poll(void);
int cyw43_arch_wifi_connect_async(const char *, const char *, unsigned);
#ifdef __cplusplus
}
#endif
#endif