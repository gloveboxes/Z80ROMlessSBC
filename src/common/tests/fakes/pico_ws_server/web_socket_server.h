#ifndef TEST_WEB_SOCKET_SERVER_H
#define TEST_WEB_SOCKET_SERVER_H
#include <cstddef>
#include <cstdint>
class WebSocketServer {
public:
  enum class SendResult { queued, would_block, closed, invalid, too_large, no_memory, error };
  enum class CloseReason : uint8_t {
    unknown, peer_tcp_closed, peer_websocket_close, invalid_websocket_input,
    pong_backpressure, tcp_write_error, tcp_output_error, tcp_error,
    close_timeout, server_abort, local_close
  };
  using ConnectCallback = void (*)(WebSocketServer &, uint32_t);
  using CloseCallback = void (*)(WebSocketServer &, uint32_t, CloseReason, int);
  using MessageCallback = void (*)(WebSocketServer &, uint32_t, const void *, size_t);
  using PongCallback = MessageCallback;
  explicit WebSocketServer(uint32_t) {}
  size_t max_message_size = 0;
  uint16_t listening_port = 0;
  void setConnectCallback(ConnectCallback) {}
  void setCloseCallback(CloseCallback) {}
  void setMessageCallback(MessageCallback) {}
  void setPongCallback(PongCallback) {}
  void setTcpNoDelay(bool) {}
  bool setMaxMessageSize(size_t bytes) {
    max_message_size = bytes;
    return bytes == 69;
  }
  bool startListening(uint16_t port) { listening_port = port; return true; }
  void poll();
  bool close(uint32_t);
  bool abort(uint32_t);
  SendResult enqueueMessage(uint32_t, const void *, size_t);
  bool flush(uint32_t);
  bool sendMessage(uint32_t, const void *, size_t);
  bool sendPing(uint32_t, const void *, size_t);
};
#endif
