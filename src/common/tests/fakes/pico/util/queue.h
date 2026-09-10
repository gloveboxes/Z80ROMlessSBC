#ifndef TEST_PICO_QUEUE_H
#define TEST_PICO_QUEUE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint8_t *data;
  size_t element_size;
  unsigned capacity;
  unsigned count;
} queue_t;
void queue_init(queue_t *queue, unsigned element_size, unsigned capacity);
bool queue_try_add(queue_t *queue, const void *item);
bool queue_try_remove(queue_t *queue, void *item);
bool queue_is_empty(queue_t *queue);
#endif