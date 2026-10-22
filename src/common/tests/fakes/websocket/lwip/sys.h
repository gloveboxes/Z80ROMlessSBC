#ifndef TEST_WS_SYS_H
#define TEST_WS_SYS_H
#include <cstdint>
inline uint32_t test_now;
inline uint32_t sys_now() { return test_now; }
#endif
