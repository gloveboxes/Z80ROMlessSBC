add_library(z80_supervisor_common STATIC
  common/supervisor.c
)
target_include_directories(z80_supervisor_common PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/common/include
)
target_link_libraries(z80_supervisor_common PUBLIC
  pico_stdlib
)
target_compile_options(z80_supervisor_common PRIVATE
  -Wall
  -Wextra
  -Werror
)

add_library(z80_clock STATIC common/clock.c)
target_include_directories(z80_clock PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_clock PUBLIC z80_supervisor_common hardware_pwm hardware_clocks)
target_compile_options(z80_clock PRIVATE -Wall -Wextra -Werror)

add_library(z80_bus STATIC common/bus.c)
target_include_directories(z80_bus PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_bus PUBLIC z80_supervisor_common)
target_compile_options(z80_bus PRIVATE -Wall -Wextra -Werror)

add_library(z80_sram STATIC common/sram.c)
target_include_directories(z80_sram PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_sram PUBLIC z80_bus z80_clock)
target_compile_options(z80_sram PRIVATE -Wall -Wextra -Werror)

add_library(z80_cpu STATIC common/cpu.c)
target_include_directories(z80_cpu PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_cpu PUBLIC z80_sram z80_clock)
target_compile_options(z80_cpu PRIVATE -Wall -Wextra -Werror)

add_library(z80_io_trap STATIC common/io_trap.c)
target_include_directories(z80_io_trap PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_io_trap PUBLIC z80_cpu hardware_watchdog)
target_compile_options(z80_io_trap PRIVATE -Wall -Wextra -Werror)

add_library(z80_flash_disk STATIC
  common/disk_device.c
  common/flash_backend.c
  common/environment_store.c
)
target_include_directories(z80_flash_disk PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_flash_disk PUBLIC
  z80_io_trap
  pico_flash
  pico_multicore
  pico_util
  hardware_flash
  hardware_watchdog
)
target_compile_options(z80_flash_disk PRIVATE -Wall -Wextra -Werror)

add_library(z80_io_services STATIC
  common/io_ports.c
  common/io_service.c
  common/io_service_platform.c
  common/http_io.c
  common/http_stream.c
  common/weather.c
  common/chat.c
  common/chat_stream.c
  cpm/dcc_io_adapter/port_drivers/json_scan.c
  cpm/dcc_io_adapter/host/PortDrivers/environment_io.c
)
target_include_directories(z80_io_services PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/common/include
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/common)
target_compile_definitions(z80_io_services PRIVATE Z80_ENV_FLASH=1)
target_compile_options(z80_io_services PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_io_services PUBLIC pico_stdlib pico_rand pico_unique_id)

add_library(z80_terminal_queue STATIC common/terminal_queue.c)
target_include_directories(z80_terminal_queue PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/common/include)
target_link_libraries(z80_terminal_queue PUBLIC pico_stdlib pico_util)
target_compile_options(z80_terminal_queue PRIVATE -Wall -Wextra -Werror)

function(add_z80_stage target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE z80_supervisor_common ${ARGN})
  set_source_files_properties(${source}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
  )
  pico_enable_stdio_usb(${target} 1)
  pico_enable_stdio_uart(${target} 0)
  pico_add_extra_outputs(${target})
endfunction()

add_subdirectory(stage01_supervisor)
add_subdirectory(stage02_buffers_clock)
add_subdirectory(stage03_mcp23s17)
add_subdirectory(stage04_address_bus)
add_subdirectory(stage05_data_bus)
add_subdirectory(stage06_sram_dma)
add_subdirectory(stage08_virtual_io)
add_subdirectory(stage09_flash_storage)
add_subdirectory(stage10_websocket_terminal)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(Z80ASM_EXECUTABLE z80asm)
if(Z80ASM_EXECUTABLE)
  function(add_cpm_images target firmware directory)
  set(cpm_output_dir ${CMAKE_BINARY_DIR}/${directory})
  add_custom_command(
    OUTPUT ${cpm_output_dir}/manifest.json
    BYPRODUCTS
      ${cpm_output_dir}/bios.bin
      ${cpm_output_dir}/AUTORUN.COM
      ${cpm_output_dir}/z80boot.img
      ${cpm_output_dir}/z80boot.pkg
      ${cpm_output_dir}/drive_a_cpm63k-z80.img
      ${cpm_output_dir}/drive_b_bdsc.img
      ${cpm_output_dir}/drive_c_escape.img
      ${cpm_output_dir}/drive_d_blank.img
      ${cpm_output_dir}/z80romless-flash.bin
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_LIST_DIR}/cpm/build_images.py
      --assembler ${Z80ASM_EXECUTABLE}
      --firmware
        $<TARGET_FILE_DIR:${firmware}>/${firmware}.bin
      --output-dir ${cpm_output_dir}
    DEPENDS
      ${firmware}
      cpm/z80_bios.asm
      cpm/autorun.asm
      cpm/cpm64_z80.asm
      cpm/build_images.py
      cpm/cpm64_system.bin
      disks/geometry.py
      disks/generated/drive_a_cpm63k.img
      disks/generated/drive_b_bdsc.img
      disks/generated/drive_c_escape.img
      disks/generated/drive_d_blank.img
    VERBATIM
  )
  add_custom_target(${target}
    DEPENDS ${cpm_output_dir}/manifest.json
  )
  endfunction()
  add_cpm_images(z80_cpm_images z80_stage10_websocket_terminal cpm)
  add_cpm_images(z80_stage09_cpm_images z80_stage09_flash_storage cpm-stage09)
  add_cpm_images(z80_stage09_cpm_loader_images z80_stage09_cpm_loader
    cpm-stage09-loader)
else()
  message(STATUS "z80asm not found; z80_cpm_images target is unavailable")
endif()
