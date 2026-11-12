find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Z80_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(Z80_CONTAINER_LAUNCHER "${Z80_PROJECT_ROOT}/scripts/z80_container.py")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(Z80_ASSEMBLER "${Python3_EXECUTABLE}" "${Z80_CONTAINER_LAUNCHER}" assemble)
  set(Z80_ASSEMBLER_AVAILABLE TRUE)
  if(NOT TARGET z80_container_image)
    add_custom_target(z80_container_image
      COMMAND "${Python3_EXECUTABLE}" "${Z80_CONTAINER_LAUNCHER}" image
      VERBATIM)
  endif()
  set(Z80_ASSEMBLER_DEPENDENCIES z80_container_image "${Z80_CONTAINER_LAUNCHER}")
else()
  find_program(Z80_HOST_ASSEMBLER z80asm)
  set(Z80_ASSEMBLER "${Z80_HOST_ASSEMBLER}")
  set(Z80_ASSEMBLER_AVAILABLE "${Z80_HOST_ASSEMBLER}")
  set(Z80_ASSEMBLER_DEPENDENCIES "")
endif()
