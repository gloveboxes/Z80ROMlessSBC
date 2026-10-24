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

typedef enum {
  Z80_WEB_TERMINAL_DISCONNECT_NONE = 0,
  Z80_WEB_TERMINAL_DISCONNECT_SOCKET_CLOSED,
  Z80_WEB_TERMINAL_DISCONNECT_NETWORK_LOST,
  Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_ACK_TIMEOUT,
  Z80_WEB_TERMINAL_DISCONNECT_PONG_TIMEOUT,
  Z80_WEB_TERMINAL_DISCONNECT_PROTOCOL_ERROR,
  Z80_WEB_TERMINAL_DISCONNECT_OUTPUT_SEND_ERROR,
} z80_web_terminal_disconnect_reason_t;

typedef enum {
  Z80_WEB_TERMINAL_WS_CLOSE_UNKNOWN = 0,
  Z80_WEB_TERMINAL_WS_CLOSE_PEER_TCP_CLOSED,
  Z80_WEB_TERMINAL_WS_CLOSE_PEER_WEBSOCKET_CLOSE,
  Z80_WEB_TERMINAL_WS_CLOSE_INVALID_INPUT,
  Z80_WEB_TERMINAL_WS_CLOSE_RECEIVE_QUEUE_MESSAGE_LIMIT,
  Z80_WEB_TERMINAL_WS_CLOSE_RECEIVE_QUEUE_BYTE_LIMIT,
  Z80_WEB_TERMINAL_WS_CLOSE_PONG_BACKPRESSURE,
  Z80_WEB_TERMINAL_WS_CLOSE_TCP_WRITE_ERROR,
  Z80_WEB_TERMINAL_WS_CLOSE_TCP_OUTPUT_ERROR,
  Z80_WEB_TERMINAL_WS_CLOSE_TCP_ERROR,
  Z80_WEB_TERMINAL_WS_CLOSE_TIMEOUT,
  Z80_WEB_TERMINAL_WS_CLOSE_SERVER_ABORT,
  Z80_WEB_TERMINAL_WS_CLOSE_LOCAL_CLOSE,
} z80_web_terminal_ws_close_reason_t;

typedef struct {
  uint32_t retransmits;
  uint32_t flush_failures;
  uint32_t ack_timeouts;
  uint32_t pong_timeouts;
  uint32_t window_frames;
  uint32_t window_frames_peak;
  uint32_t retained_bytes;
  uint32_t retained_bytes_peak;
  uint32_t staged_bytes;
  uint32_t last_disconnect_reason;
  uint32_t last_ws_close_reason;
  int32_t last_ws_transport_error;
  uint32_t last_ws_queue_messages;
  uint32_t last_ws_queue_bytes;
  uint32_t last_ws_queue_peak_messages;
  uint32_t last_ws_queue_peak_bytes;
  uint32_t last_ws_queue_message_limit;
  uint32_t last_ws_queue_byte_limit;
  // Set only when the requested snapshot belongs to a live connection.
  uint32_t rx_queue_active;
  uint32_t rx_queue_messages;
  uint32_t rx_queue_bytes;
  uint32_t rx_queue_peak_messages;
  uint32_t rx_queue_peak_bytes;
  uint32_t rx_queue_message_limit;
  uint32_t rx_queue_byte_limit;
} z80_web_terminal_stats_t;

void z80_web_terminal_poll(bool online, uint32_t generation);
z80_web_terminal_stats_t z80_web_terminal_get_stats(void);
#ifdef __cplusplus
}
#endif
#endif
