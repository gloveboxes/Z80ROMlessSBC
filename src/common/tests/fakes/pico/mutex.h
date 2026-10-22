#ifndef TEST_PICO_MUTEX_H
#define TEST_PICO_MUTEX_H
#include <assert.h>
#include <stdbool.h>

typedef struct {
  bool locked;
} mutex_t;

#define auto_init_mutex(name) static mutex_t name = {false}

static inline void mutex_enter_blocking(mutex_t *mutex) {
  assert(!mutex->locked);
  mutex->locked = true;
}

static inline void mutex_exit(mutex_t *mutex) {
  assert(mutex->locked);
  mutex->locked = false;
}
#endif
