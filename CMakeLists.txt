cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_PATH "/Users/dave/GitHub/pico/pico-sdk" CACHE PATH
    "Path to the Raspberry Pi Pico SDK")
endif()

set(PICO_BOARD pico2_w CACHE STRING "Pico board type")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(z80_romless_sbc C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Keep every firmware image below the journal/storage partition. This changes
# the linker region only; PICO_FLASH_SIZE_BYTES remains the board's physical
# 4 MiB value for hardware bounds checks and layout assertions.
file(WRITE "${CMAKE_BINARY_DIR}/pico_flash_region.ld"
  "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 0x290000\n")

add_subdirectory(src)