#ifndef TEST_WS_PBUF_H
#define TEST_WS_PBUF_H
#include <cstddef>
struct pbuf {
  pbuf* next;
  void* payload;
  unsigned short tot_len;
  unsigned short len;
};
inline unsigned pbuf_frees;
inline unsigned char pbuf_free(pbuf*) { ++pbuf_frees; return 1; }
#endif
