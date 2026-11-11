#ifndef Z80_WEB_TERMINAL_H
#define Z80_WEB_TERMINAL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
  Z80_WEB_TERMINAL_PORT = 80,
  Z80_WEB_TERMINAL_FRAME_BYTES = 512,
  Z80_WEB_TERMINAL_OUTPUT_WINDOW = 4,
};

typedef enum {
  Z80_WEB_TERMINAL_DISCONNECT_NONE = 0,
  Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED,
  Z80_WEB_TERMINAL_DISCONNECT_NETWORK_LOST,
  Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_ACK_TIMEOUT,
  Z80_WEB_TERMINAL_DISCONNECT_PONG_TIMEOUT,
  Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR,
  Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR,
} z80_web_terminal_disconnect_reason_t;

/* Core 1 publishes each field; any core may read a snapshot. */
typedef struct {
  uint32_t flush_failures;
  uint32_t ack_timeouts;
  uint32_t pong_timeouts;
  /* Output frames sent and not yet consumed by the browser. */
  uint32_t window_frames;
  uint32_t window_frames_peak;
  /* Output bytes waiting to be framed. */
  uint32_t staged_bytes;
  uint32_t last_disconnect_reason;
  /* The transport's reason for the last socket close; 0 when it gave none. */
  uint32_t last_ws_close_reason;
  int32_t last_ws_transport_error;
} z80_web_terminal_stats_t;

/* Atomic handoff: WebSocket/core 1 requests; supervisor/core 0 publishes. */
typedef enum {
  Z80_WEB_REBOOT_IDLE = 0,
  Z80_WEB_REBOOT_REQUESTED = 1,
  Z80_WEB_REBOOT_FLUSHING = 2,
  Z80_WEB_REBOOT_RESTARTING = 3,
  Z80_WEB_REBOOT_STORAGE_ERROR = 4,
  Z80_WEB_REBOOT_TIMEOUT = 5,
  Z80_WEB_REBOOT_REJECTED = 6,
  Z80_WEB_REBOOT_CPM_READY = 7,
} z80_web_reboot_status_t;
z80_web_reboot_status_t z80_web_terminal_reboot_status(void);
/* Claim a live request; an expired request must never start a late reboot. */
bool z80_web_terminal_begin_reboot(void);
void z80_web_terminal_set_reboot_status(z80_web_reboot_status_t status);

void z80_web_terminal_poll(bool online, uint32_t generation);
z80_web_terminal_stats_t z80_web_terminal_get_stats(void);
const char *z80_web_terminal_disconnect_name(uint32_t reason);
const char *z80_web_terminal_ws_close_name(uint32_t reason);
#ifdef __cplusplus
}
#endif
#endif
