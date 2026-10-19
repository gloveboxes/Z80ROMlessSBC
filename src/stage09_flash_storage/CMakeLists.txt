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

add_z80_stage(z80_stage09_flash_storage main.c
  z80_flash_disk_20mhz z80_terminal_queue pico_multicore)
include(disk_diagnostic_build.cmake)
target_sources(z80_stage09_flash_storage PRIVATE "${diagnostic_header}")
target_include_directories(z80_stage09_flash_storage PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(z80_stage09_flash_storage PRIVATE
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000)