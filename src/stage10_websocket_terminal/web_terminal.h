#ifndef Z80_WEB_TERMINAL_H
#define Z80_WEB_TERMINAL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
  Z80_WEB_TERMINAL_PORT = 8088,
  Z80_WEB_TERMINAL_FRAME_BYTES = 512,
  Z80_WEB_TERMINAL_OUTPUT_WINDOW = 4,
};

typedef struct {
  uint32_t output_frames;
  uint32_t output_bytes;
  uint32_t retransmits;
  uint32_t write_blocks;
  uint32_t flush_failures;
  uint32_t window_full_polls;
  uint32_t window_wait_max_us;
  uint32_t ack_max_us;
  uint32_t timeouts;
  uint32_t window_frames;
  uint32_t window_frames_peak;
  uint32_t retained_bytes;
  uint32_t retained_bytes_peak;
  uint32_t staged_bytes;
  uint32_t frame_bytes_max;
  uint32_t small_frames;
  uint32_t full_frames;
} z80_web_terminal_stats_t;

void z80_web_terminal_poll(bool online, uint32_t generation);
z80_web_terminal_stats_t z80_web_terminal_get_stats(void);
#ifdef __cplusplus
}
#endif
#endif
