#ifndef Z80_MATH_SOAK_PROGRAM_H
#define Z80_MATH_SOAK_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
  SOAK_IMAGE_SIZE = 0x2000,
  SOAK_VECTOR_BASE = 0x1000,
  SOAK_VECTOR_SIZE = 20,
  SOAK_VECTOR_COUNT = 128,
  SOAK_REPEATS = 16,
  SOAK_ASSERTIONS_PER_VECTOR = 11,
  SOAK_STACK_CHECKS = 5,
  SOAK_LOOP_CHECKS = SOAK_REPEATS + 1,
  SOAK_WORK_BYTES = 0xE000,
};

static inline void soak_word(uint8_t *destination, uint16_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

static inline uint8_t soak_flags(uint16_t a, uint16_t b, unsigned carry,
                                 bool subtract) {
  int32_t wide = subtract ? (int32_t)a - b - (int32_t)carry
                          : (int32_t)a + b + (int32_t)carry;
  uint16_t result = (uint16_t)wide;
  int32_t signed_a = a < 0x8000 ? a : (int32_t)a - 65536;
  int32_t signed_b = b < 0x8000 ? b : (int32_t)b - 65536;
  int32_t signed_result = subtract ? signed_a - signed_b - (int32_t)carry
                                   : signed_a + signed_b + (int32_t)carry;
  bool half = subtract ? (a & 0xFFFu) < (b & 0xFFFu) + carry
                       : (a & 0xFFFu) + (b & 0xFFFu) + carry > 0xFFFu;
  return (uint8_t)((result & 0x8000u ? 0x80 : 0) |
                   (result == 0 ? 0x40 : 0) | (half ? 0x10 : 0) |
                   (signed_result < -32768 || signed_result > 32767 ? 4 : 0) |
                   (subtract ? 2 : 0) | (wide < 0 || wide > 65535 ? 1 : 0));
}

static inline uint32_t soak_random(uint32_t *state) {
  *state ^= *state << 13;
  *state ^= *state >> 17;
  *state ^= *state << 5;
  return *state;
}

static inline void soak_build_image(uint8_t image[SOAK_IMAGE_SIZE],
                                    const uint8_t *code, size_t code_size) {
  static const uint16_t edges[] = {
      0, 1, 0x0FFF, 0x7FFF, 0x8000, 0x8001, 0x5555, 0xFFFF};
  uint32_t seed = 0x5A801337;
  memset(image, 0xA5, SOAK_IMAGE_SIZE);
  memcpy(image, code, code_size);
  for (unsigned i = 0; i < SOAK_VECTOR_COUNT; ++i) {
    uint16_t a = i < 64 ? edges[i / 8] : (uint16_t)soak_random(&seed);
    uint16_t b = i < 64 ? edges[i % 8] : (uint16_t)soak_random(&seed);
    uint8_t *v = image + SOAK_VECTOR_BASE + i * SOAK_VECTOR_SIZE;
    soak_word(v, a);
    soak_word(v + 2, b);
    soak_word(v + 4, (uint16_t)((uint32_t)a + b));
    v[6] = soak_flags(a, b, 0, false) & 0x13;
    soak_word(v + 7, (uint16_t)((uint32_t)a + b + 1));
    v[9] = soak_flags(a, b, 1, false);
    soak_word(v + 10, (uint16_t)((int32_t)a - b));
    v[12] = soak_flags(a, b, 0, true);
    soak_word(v + 13, (uint16_t)((int32_t)a - b - 1));
    v[15] = soak_flags(a, b, 1, true);
    soak_word(v + 16, (uint16_t)((uint32_t)a * b));
    v[18] = a < b ? 0xFF : a > b ? 1 : 0;
    int32_t sa = a < 0x8000 ? a : (int32_t)a - 65536;
    int32_t sb = b < 0x8000 ? b : (int32_t)b - 65536;
    v[19] = sa < sb ? 0xFF : sa > sb ? 1 : 0;
  }
}

#endif
