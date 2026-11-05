cmake_minimum_required(VERSION 3.18)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(PICO_BOARD pico2_w CACHE STRING "Pico board type")
  if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "/Users/dave/GitHub/pico/pico-sdk" CACHE PATH
      "Path to the Raspberry Pi Pico SDK")
  endif()
  include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
  project(stage11_extension_board C CXX ASM)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_CXX_STANDARD 17)
  pico_sdk_init()
endif()

add_subdirectory(flash_ATTiny85)
add_subdirectory(test_ATTiny85)
