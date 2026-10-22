#ifndef TEST_WS_CYW43_H
#define TEST_WS_CYW43_H
#include <cassert>
inline unsigned test_cyw43_depth;
inline void cyw43_thread_enter() { ++test_cyw43_depth; }
inline void cyw43_thread_exit() { assert(test_cyw43_depth); --test_cyw43_depth; }
inline void cyw43_arch_lwip_check() { assert(test_cyw43_depth); }
#endif
