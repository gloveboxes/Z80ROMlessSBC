#ifndef TEST_WEB_SOCKET_SERVER_H
#define TEST_WEB_SOCKET_SERVER_H
#include <cstddef>
#include <cstdint>
class WebSocketServer {
public:
  using ConnectCallback = void (*)(WebSocketServer &, uint32_t);
  using CloseCallback = ConnectCallback;
  using MessageCallback = void (*)(WebSocketServer &, uint32_t, const void *, size_t);
  explicit WebSocketServer(uint32_t) {}
  void setConnectCallback(ConnectCallback) {}
  void setCloseCallback(CloseCallback) {}
  void setMessageCallback(MessageCallback) {}
  void setTcpNoDelay(bool) {}
  bool startListening(uint16_t) { return true; }
  void popMessages();
  bool close(uint32_t);
  bool sendMessage(uint32_t, const void *, size_t);
};
#endif