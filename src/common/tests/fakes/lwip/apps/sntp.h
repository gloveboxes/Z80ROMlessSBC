#ifndef TEST_LWIP_SNTP_H
#define TEST_LWIP_SNTP_H
enum { SNTP_OPMODE_POLL = 0 };
void sntp_setoperatingmode(unsigned char mode);
void sntp_setservername(unsigned char index, const char *name);
void sntp_init(void);
#endif
