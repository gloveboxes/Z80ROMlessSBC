#ifndef TEST_WS_PBUF_H
#define TEST_WS_PBUF_H
#include <cstddef>
struct pbuf { size_t tot_len; };
static inline unsigned char pbuf_get_at(const pbuf *, size_t) { return 0; }
#endif
