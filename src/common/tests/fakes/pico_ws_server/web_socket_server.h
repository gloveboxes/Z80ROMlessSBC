#ifndef TEST_WEB_SOCKET_SERVER_H
#define TEST_WEB_SOCKET_SERVER_H
#include <cstddef>
#include <cstdint>
class WebSocketServer {
public:
  enum class SendResult { queued, would_block, closed, invalid, too_large, no_memory, error };
  struct ReceiveLimits {
    size_t messageBytes = 64000, queuedBytes = 64000, queuedMessages = 16, dispatchMessages = 8;
  };
  struct ReceiveQueueStats {
    size_t queuedMessages = 0, queuedBytes = 0, peakQueuedMessages = 0,
           peakQueuedBytes = 0, messageLimit = 0, byteLimit = 0;
  };
  enum class CloseReason : uint8_t {
    unknown, peer_tcp_closed, peer_websocket_close, invalid_websocket_input,
    receive_queue_message_limit, receive_queue_byte_limit, pong_backpressure,
    tcp_write_error, tcp_output_error, tcp_error, close_timeout, server_abort, local_close
  };
  using ConnectCallback = void (*)(WebSocketServer &, uint32_t);
  using CloseCallback = ConnectCallback;
  using CloseDetailsCallback = void (*)(WebSocketServer &, uint32_t, CloseReason,
                                      int, size_t, size_t, size_t, size_t,
                                      size_t, size_t);
  using MessageCallback = void (*)(WebSocketServer &, uint32_t, const void *, size_t);
  using PongCallback = MessageCallback;
  explicit WebSocketServer(uint32_t) {}
  ReceiveLimits receive_limits;
  ReceiveQueueStats receive_queue_stats;
  CloseDetailsCallback close_details_callback = nullptr;
  void setConnectCallback(ConnectCallback) {}
  void setCloseCallback(CloseCallback) {}
  void setCloseDetailsCallback(CloseDetailsCallback cb) { close_details_callback = cb; }
  void setMessageCallback(MessageCallback) {}
  void setPongCallback(PongCallback) {}
  void setTcpNoDelay(bool) {}
  bool setReceiveLimits(const ReceiveLimits& limits) {
    receive_limits = limits;
    return limits.messageBytes == 69 && limits.queuedBytes == 1024 &&
           limits.queuedMessages == 64 && limits.dispatchMessages == 32;
  }
  bool getReceiveQueueStats(uint32_t, ReceiveQueueStats&);
  bool startListening(uint16_t) { return true; }
  void popMessages();
  bool close(uint32_t);
  bool abort(uint32_t);
  SendResult enqueueMessage(uint32_t, const void *, size_t);
  bool flush(uint32_t);
  bool sendMessage(uint32_t, const void *, size_t);
  bool sendPing(uint32_t, const void *, size_t);
};
#endif
