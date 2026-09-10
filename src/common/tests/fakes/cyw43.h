#ifndef TEST_CYW43_H
#define TEST_CYW43_H
enum { CYW43_ITF_STA, CYW43_AUTH_WPA2_AES_PSK, CYW43_NO_POWERSAVE_MODE };
enum { CYW43_LINK_DOWN = 0, CYW43_LINK_UP = 3 };
typedef struct { int unused; } cyw43_t;
extern cyw43_t cyw43_state;
int cyw43_tcpip_link_status(cyw43_t *, int);
int cyw43_wifi_pm(cyw43_t *, int);
int cyw43_wifi_leave(cyw43_t *, int);
#endif