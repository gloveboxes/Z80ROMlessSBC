#ifndef Z80SBC_ENVIRONMENT_H
#define Z80SBC_ENVIRONMENT_H

#include <stddef.h>
#include <stdint.h>

enum { Z80_ENV_SNAPSHOT_BYTES = 3840 };

/* Core 1 only. Data is a sequence of NUL-terminated key/value pairs.
 * Return 0, -2 (read), -3 (write), or -4 (capacity), matching ENV status.
 * A successful write is durable before returning.
 */
int z80_env_snapshot_load(uint8_t *data, size_t capacity, size_t *length);
int z80_env_snapshot_save(const uint8_t *data, size_t length);

#endif
