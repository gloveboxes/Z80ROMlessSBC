#ifndef TEST_PICO_CRITICAL_SECTION_H
#define TEST_PICO_CRITICAL_SECTION_H
#include <assert.h>
#include <stdbool.h>

typedef struct {
  bool initialized;
  bool locked;
} critical_section_t;

static inline bool critical_section_is_initialized(critical_section_t *section) {
  return section->initialized;
}

static inline void critical_section_init(critical_section_t *section) {
  section->initialized = true;
  section->locked = false;
}

static inline void critical_section_enter_blocking(critical_section_t *section) {
  assert(section->initialized && !section->locked);
  section->locked = true;
}

static inline void critical_section_exit(critical_section_t *section) {
  assert(section->locked);
  section->locked = false;
}
#endif
