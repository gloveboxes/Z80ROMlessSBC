add_library(z80_flash_disk_20mhz STATIC
  ../common/disk_device.c
  ../common/flash_backend.c
)
target_include_directories(z80_flash_disk_20mhz PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/../common/include)
target_compile_definitions(z80_flash_disk_20mhz PUBLIC
  PICO_FLASH_ASSUME_CORE1_SAFE=1)
target_compile_options(z80_flash_disk_20mhz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_flash_disk_20mhz PUBLIC
  z80_pwm_20mhz pico_flash pico_multicore pico_util hardware_flash
  hardware_watchdog)

set_source_files_properties(ansi_input.c PROPERTIES
  COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
include(disk_diagnostic_build.cmake)
foreach(stage09_target IN ITEMS
    z80_stage09_flash_storage z80_stage09_cpm_loader)
  add_z80_stage(${stage09_target} main.c
    z80_flash_disk_20mhz z80_terminal_queue pico_multicore)
  target_sources(${stage09_target} PRIVATE ansi_input.c "${diagnostic_header}")
  target_include_directories(${stage09_target} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_definitions(${stage09_target} PRIVATE
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000)
endforeach()
target_compile_definitions(z80_stage09_cpm_loader PRIVATE
  Z80_STAGE09_AUTOBOOT=1
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0)