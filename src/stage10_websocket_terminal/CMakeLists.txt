include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(Z80_WIFI_SSID "" CACHE STRING "Wi-Fi SSID for Stage 10")
set(Z80_WIFI_PASSWORD "" CACHE STRING "Wi-Fi password for Stage 10")

set(stage10_generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${stage10_generated_dir})
configure_file(wifi_config.h.in ${stage10_generated_dir}/wifi_config.h @ONLY)

add_custom_command(
  OUTPUT ${stage10_generated_dir}/static_html_hex.h
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/embed_html.py
          ${CMAKE_CURRENT_LIST_DIR}/terminal.html
          ${stage10_generated_dir}/static_html_hex.h
  DEPENDS terminal.html embed_html.py
  VERBATIM
)
add_custom_target(z80_terminal_html
  DEPENDS ${stage10_generated_dir}/static_html_hex.h
)

FetchContent_Declare(z80_ws_source
  GIT_REPOSITORY https://github.com/cadouthat/pico-ws-server.git
  GIT_TAG 58f780d4b68c6c592f4d9eb3421cf17c25d3910b
  SOURCE_SUBDIR _z80_no_cmake
)
FetchContent_MakeAvailable(z80_ws_source)

if(NOT TARGET lwipopts_provider)
  add_library(lwipopts_provider INTERFACE)
  target_include_directories(lwipopts_provider INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
  )
endif()

add_library(z80_pico_ws_server STATIC
  ${z80_ws_source_SOURCE_DIR}/src/client_connection.cpp
  ${z80_ws_source_SOURCE_DIR}/src/http_handler.cpp
  ${z80_ws_source_SOURCE_DIR}/src/web_socket_frame_builder.cpp
  ${z80_ws_source_SOURCE_DIR}/src/web_socket_handler.cpp
  ${z80_ws_source_SOURCE_DIR}/src/web_socket_message_builder.cpp
  ${z80_ws_source_SOURCE_DIR}/src/web_socket_server.cpp
  ${z80_ws_source_SOURCE_DIR}/src/web_socket_server_internal.cpp
)
add_dependencies(z80_pico_ws_server z80_terminal_html)
target_include_directories(z80_pico_ws_server
  PUBLIC ${z80_ws_source_SOURCE_DIR}/include
  PRIVATE ${z80_ws_source_SOURCE_DIR}/src ${stage10_generated_dir}
)
target_compile_definitions(z80_pico_ws_server PRIVATE DEBUG_PRINT=0)
target_link_libraries(z80_pico_ws_server PUBLIC
  pico_stdlib
  pico_cyw43_driver
  pico_lwip_nosys
  pico_mbedtls
  lwipopts_provider
)

add_library(z80_terminal STATIC
  ${CMAKE_CURRENT_LIST_DIR}/../common/terminal_bridge.c
  ${CMAKE_CURRENT_LIST_DIR}/../common/terminal_network.cpp
)
set_source_files_properties(
  ${CMAKE_CURRENT_LIST_DIR}/../common/terminal_bridge.c
  ${CMAKE_CURRENT_LIST_DIR}/../common/terminal_network.cpp
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
)
target_include_directories(z80_terminal PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/../common/include
  ${stage10_generated_dir}
)
target_link_libraries(z80_terminal PUBLIC
  z80_flash_disk
  z80_pico_ws_server
  pico_cyw43_arch_lwip_threadsafe_background
  pico_util
)

add_z80_stage(z80_stage10_websocket_terminal main.c
  z80_terminal
  pico_multicore
)
target_compile_definitions(z80_stage10_websocket_terminal PRIVATE
  MBEDTLS_ALLOW_PRIVATE_ACCESS=1
  MBEDTLS_HAVE_TIME=1
  MBEDTLS_PLATFORM_MS_TIME_ALT=1
)