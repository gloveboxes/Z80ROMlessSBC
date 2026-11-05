find_program(AVR_GCC avr-gcc REQUIRED)
find_program(AVR_OBJCOPY avr-objcopy REQUIRED)

set(target_elf ${CMAKE_CURRENT_BINARY_DIR}/attiny85.elf)
set(target_binary ${CMAKE_CURRENT_BINARY_DIR}/attiny85.bin)
set(target_header ${CMAKE_CURRENT_BINARY_DIR}/attiny85_firmware.h)

add_custom_command(
  OUTPUT ${target_elf} ${target_binary} ${target_header}
  COMMAND ${AVR_GCC} -mmcu=attiny85 -DF_CPU=8000000UL -std=c11 -Os
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections
    -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/attiny85.map
    ${CMAKE_CURRENT_LIST_DIR}/target/main.c -o ${target_elf}
  COMMAND ${AVR_OBJCOPY} -O binary -R .eeprom ${target_elf} ${target_binary}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${target_binary} -DOUTPUT=${target_header}
    -P ${CMAKE_CURRENT_LIST_DIR}/embed_firmware.cmake
  DEPENDS target/main.c target/protocol.h embed_firmware.cmake
  VERBATIM
)
add_custom_target(stage11_attiny85_firmware DEPENDS ${target_header})

add_executable(stage11_flash_attiny85 main.c ${target_header})
target_include_directories(stage11_flash_attiny85 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(stage11_flash_attiny85 PRIVATE pico_stdlib hardware_spi)
target_compile_options(stage11_flash_attiny85 PRIVATE -Wall -Wextra -Werror)
pico_enable_stdio_usb(stage11_flash_attiny85 1)
pico_enable_stdio_uart(stage11_flash_attiny85 0)
pico_add_extra_outputs(stage11_flash_attiny85)
