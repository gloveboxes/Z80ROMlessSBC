#include <cassert>
#include <cstdio>

#include "client_connection.h"
#include "web_socket_handler.h"

int main() {
  for (unsigned failure = 0; failure < 3; ++failure) {
    ClientConnection connection;
    connection.write_ok = failure != 1;
    connection.flush_ok = failure != 2;
    WebSocketHandler handler(connection);
    assert(!handler.isClosing());
    assert(handler.close() == (failure == 0));
    assert(handler.isClosing());
    assert(handler.close());
    assert(connection.writes == 1);
    assert(!handler.sendMessage(WebSocketMessage(WebSocketMessage::BINARY, "X", 1)));
  }
  puts("PASS: real WebSocket close enters cleanup state even on write/flush failure");
}
