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
  using ConnectCallback = void (*)(WebSocketServer &, uint32_t);
  using CloseCallback = ConnectCallback;
  using MessageCallback = void (*)(WebSocketServer &, uint32_t, const void *, size_t);
  using PongCallback = MessageCallback;
  explicit WebSocketServer(uint32_t) {}
  void setConnectCallback(ConnectCallback) {}
  void setCloseCallback(CloseCallback) {}
  void setMessageCallback(MessageCallback) {}
  void setPongCallback(PongCallback) {}
  void setTcpNoDelay(bool) {}
  bool setReceiveLimits(const ReceiveLimits& limits) {
    return limits.messageBytes == 69 && limits.queuedBytes == 1024 &&
           limits.queuedMessages == 16 && limits.dispatchMessages == 8;
  }
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