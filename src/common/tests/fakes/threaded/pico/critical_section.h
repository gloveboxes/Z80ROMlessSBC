#ifndef TEST_THREADED_CRITICAL_SECTION_H
#define TEST_THREADED_CRITICAL_SECTION_H
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
typedef struct {
  bool initialized;
  pthread_mutex_t mutex;
} critical_section_t;
static inline bool critical_section_is_initialized(critical_section_t *section) {
  return section->initialized;
}
static inline void critical_section_init(critical_section_t *section) {
  assert(pthread_mutex_init(&section->mutex, NULL) == 0);
  section->initialized = true;
}
static inline void critical_section_enter_blocking(critical_section_t *section) {
  assert(pthread_mutex_lock(&section->mutex) == 0);
}
static inline void critical_section_exit(critical_section_t *section) {
  assert(pthread_mutex_unlock(&section->mutex) == 0);
}
#endif
