#ifndef TEST_WS_CLIENT_CONNECTION_H
#define TEST_WS_CLIENT_CONNECTION_H

#include "web_socket_message.h"

class ClientConnection {
public:
  bool write_ok = true;
  bool flush_ok = true;
  unsigned writes = 0;
  bool sendRaw(const void *, size_t) { ++writes; return write_ok; }
  bool flushSend() { return flush_ok; }
  void processWebSocketMessage(WebSocketMessage &&) {}
  void processWebSocketPong(const void *, size_t) {}
};
#endif
