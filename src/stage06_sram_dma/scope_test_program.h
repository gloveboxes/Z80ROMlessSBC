#ifndef Z80_SCOPE_TEST_PROGRAM_H
#define Z80_SCOPE_TEST_PROGRAM_H

#include "math_soak_program.h"
#include "scope_test_image.h"

enum {
  SCOPE_MAILBOX_BASE = 0x0F00,
  SCOPE_MAILBOX_SIZE = 32,
  SCOPE_STATE = 4,
  SCOPE_BUSY = 5,
  SCOPE_PHASES = 6,
  SCOPE_PATTERN = 10,
  SCOPE_STAGE = 12,
  SCOPE_VECTOR = 13,
  SCOPE_ADDRESS = 14,
  SCOPE_ACTUAL = 16,
  SCOPE_EXPECTED = 18,
  SCOPE_RUNNING = 1,
  SCOPE_FAILED = 0xE1,
};
_Static_assert(sizeof(z80_scope_code) <= SCOPE_MAILBOX_BASE,
               "Scope code must not overlap its mailbox");
_Static_assert(SCOPE_MAILBOX_BASE + SCOPE_MAILBOX_SIZE <= SOAK_VECTOR_BASE,
               "Scope mailbox must not overlap the reference table");

static inline void scope_build_image(uint8_t image[SOAK_IMAGE_SIZE]) {
  soak_build_image(image, z80_scope_code, sizeof(z80_scope_code));
  uint8_t *mailbox = image + SCOPE_MAILBOX_BASE;
  memset(mailbox, 0, SCOPE_MAILBOX_SIZE);
  memcpy(mailbox, "SCP1", 4);
  mailbox[SCOPE_PATTERN] = mailbox[SCOPE_VECTOR] = 0xFF;
}

static inline uint16_t scope_read_word(const uint8_t *bytes) {
  return (uint16_t)(bytes[0] | (unsigned)bytes[1] << 8);
}

static inline uint32_t scope_read_phases(const uint8_t *mailbox) {
  return (uint32_t)scope_read_word(mailbox + SCOPE_PHASES) |
         (uint32_t)scope_read_word(mailbox + SCOPE_PHASES + 2) << 16;
}

static inline bool scope_snapshot_valid(const uint8_t *mailbox) {
  uint32_t phases = scope_read_phases(mailbox);
  if (memcmp(mailbox, "SCP1", 4) != 0 || mailbox[SCOPE_BUSY] != 0 ||
      (mailbox[SCOPE_STATE] != SCOPE_RUNNING &&
       mailbox[SCOPE_STATE] != SCOPE_FAILED) ||
      mailbox[SCOPE_PATTERN] != (phases ? (phases - 1) % 4 : 0xFF))
    return false;
  if (mailbox[SCOPE_STATE] == SCOPE_FAILED) {
    uint8_t stage = mailbox[SCOPE_STAGE];
    if (!((stage >= 1 && stage <= 0x12) || (stage >= 0x80 && stage <= 0x83)) ||
        scope_read_word(mailbox + SCOPE_ACTUAL) ==
            scope_read_word(mailbox + SCOPE_EXPECTED))
      return false;
    if (stage <= 0x0B && mailbox[SCOPE_VECTOR] >= SOAK_VECTOR_COUNT)
      return false;
    if (stage >= 0x80 &&
        (mailbox[SCOPE_VECTOR] != 0xFF ||
         scope_read_word(mailbox + SCOPE_ADDRESS) < SOAK_IMAGE_SIZE ||
         mailbox[SCOPE_ACTUAL + 1] != 0 || mailbox[SCOPE_EXPECTED + 1] != 0))
      return false;
  }
  return true;
}

#endif
