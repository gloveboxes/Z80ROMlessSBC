find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(stage10_generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${stage10_generated_dir})
file(READ ${CMAKE_CURRENT_LIST_DIR}/weather_ca.pem weather_ca)
configure_file(weather_ca.h.in ${stage10_generated_dir}/weather_ca.h @ONLY)
file(READ ${CMAKE_CURRENT_LIST_DIR}/chat_ca.pem chat_ca)
configure_file(chat_ca.h.in ${stage10_generated_dir}/chat_ca.h @ONLY)
target_compile_definitions(pico_mbedtls_headers INTERFACE
  MBEDTLS_USER_CONFIG_FILE="weather_tls_config.h")
add_custom_command(
  OUTPUT ${stage10_generated_dir}/static_html_hex.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/embed_html.py
          ${CMAKE_CURRENT_LIST_DIR}/Terminal/index.html
          ${stage10_generated_dir}/static_html_hex.h
  DEPENDS Terminal/index.html embed_html.py
  VERBATIM
)
add_custom_target(z80_terminal_html
  DEPENDS ${stage10_generated_dir}/static_html_hex.h)

set(ws_source ${CMAKE_CURRENT_LIST_DIR}/lib/pico-ws-server)
if(NOT EXISTS ${ws_source}/include/pico_ws_server/web_socket_server.h)
  message(FATAL_ERROR "Initialize submodules: git submodule update --init --recursive")
endif()
add_library(lwipopts_provider INTERFACE)
target_include_directories(lwipopts_provider INTERFACE
  ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../common/include)

add_library(z80_pico_ws_server STATIC
  ${ws_source}/src/client_connection.cpp
  ${ws_source}/src/http_handler.cpp
  ${ws_source}/src/web_socket_frame_builder.cpp
  ${ws_source}/src/web_socket_handler.cpp
  ${ws_source}/src/web_socket_message_builder.cpp
  ${ws_source}/src/web_socket_server.cpp
  ${ws_source}/src/web_socket_server_internal.cpp)
add_dependencies(z80_pico_ws_server z80_terminal_html)
target_include_directories(z80_pico_ws_server
  PUBLIC ${ws_source}/include
  PRIVATE ${ws_source}/src ${stage10_generated_dir})
target_compile_definitions(z80_pico_ws_server PRIVATE DEBUG_PRINT=0)
target_link_libraries(z80_pico_ws_server PUBLIC
  pico_stdlib pico_cyw43_arch_lwip_poll pico_mbedtls lwipopts_provider)

add_library(z80_terminal STATIC
  network_services.c network_runtime.cpp
  web_terminal.cpp
  ../common/weather_http.c
  weather_network.cpp
  chat_network.cpp
  network_setup.c)
target_include_directories(z80_terminal PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${stage10_generated_dir}
  ${CMAKE_CURRENT_LIST_DIR}/../common
  ${CMAKE_CURRENT_LIST_DIR}/../common/include)
set_source_files_properties(
  network_services.c network_runtime.cpp web_terminal.cpp
  ../common/weather_http.c weather_network.cpp chat_network.cpp network_setup.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
target_link_libraries(z80_terminal PUBLIC
  z80_terminal_queue z80_io_services z80_pico_ws_server pico_lwip_sntp
  pico_lwip_mbedtls)

# Compile the tested automatic-loader application, not a parallel implementation.
include(../stage09_flash_storage/disk_diagnostic_build.cmake)
add_z80_stage(z80_stage10_websocket_terminal ../stage09_flash_storage/main.c
  z80_io_services z80_flash_disk_20mhz z80_terminal pico_multicore)
target_sources(z80_stage10_websocket_terminal PRIVATE
  ../stage09_flash_storage/ansi_input.c ../stage09_flash_storage/usb_console.c
  ../common/onboard_led.c
  "${diagnostic_header}")
target_include_directories(z80_stage10_websocket_terminal PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(z80_stage10_websocket_terminal PRIVATE
  Z80_STAGE09_AUTOBOOT=1 Z80_STAGE10_NETWORK=1
  PICO_RUNTIME_SKIP_INIT_DEFAULT_ALARM_POOL=1
  PICO_STDOUT_MUTEX=0
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000)
