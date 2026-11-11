/**
 * @file environment_io.h
 * @brief Shared environment command parser and backend entry points.
 *
 * The host uses a text file and mutex. Z80_ENV_FLASH builds use the Pico's
 * durable snapshots and are called only from core 1.
 *
 * The store is read once, on first use. Every lookup and command then works
 * on the in-memory entries, which a commit keeps identical to storage, so
 * callers never reload: they check environment_io_load_status() and cache
 * derived settings against environment_io_generation().
 *
 * Private legacy selectors (not physical Pico ports):
 *   OUT 71, 0      reset request buffer
 *   OUT 71, cmd    execute ENV command/API request
 *   OUT 72, byte   append request byte
 *   IN  71         last ENV status code
 *   IN  200        response bytes, NUL-terminated by io_ports
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ENVIRONMENT_PORT_COMMAND 71
#define ENVIRONMENT_PORT_DATA    72

/**
 * Initialize the environment driver. file_path is copied; if NULL or empty,
 * "altair_env.txt" in the current directory is used. Safe to call multiple
 * times; only the first call takes effect.
 */
void environment_io_init(const char *file_path);
void environment_io_close(void);

/**
 * Look up an environment variable from the in-memory snapshot. key is
 * matched case-insensitively (stored uppercase). Returns true and fills
 * value/value_length if found; otherwise returns false and writes "" if
 * value/value_length is non-NULL.
 */
bool environment_io_get(const char *key, char *value, size_t value_length);

/**
 * Status of reading the store, loading it on first use: 0 when the entries
 * are usable, otherwise the ENV status code of the failed read.
 */
int environment_io_load_status(void);

/**
 * Change counter for the stored environment: starts at 1 and advances on
 * every commit, so a cached setting needs reloading only when it differs.
 */
uint32_t environment_io_generation(void);

size_t environment_output(int port, uint8_t data, char *buffer, size_t buffer_length);
uint8_t environment_input(uint8_t port);

#ifdef __cplusplus
}
#endif
