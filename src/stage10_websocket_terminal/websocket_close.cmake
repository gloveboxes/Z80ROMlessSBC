function(z80_configure_websocket_handler source output)
  file(READ "${source}" handler)
  set(original "  if (!message_builder.sendMessage(WebSocketMessage(WebSocketMessage::CLOSE, nullptr, 0))) {
    return false;
  }

  is_closing = true;
  return true;")
  set(replacement "  bool sent = message_builder.sendMessage(WebSocketMessage(WebSocketMessage::CLOSE, nullptr, 0));
  is_closing = true;
  return sent;")
  string(FIND "${handler}" "${original}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "WebSocket close implementation changed; review the local cleanup fix")
  endif()
  string(REPLACE "${original}" "${replacement}" handler "${handler}")
  get_filename_component(directory "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${directory}")
  file(GENERATE OUTPUT "${output}" CONTENT "${handler}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()
