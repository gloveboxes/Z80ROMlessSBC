find_program(STORAGE_Z80ASM z80asm)
if(NOT STORAGE_Z80ASM)
  message(FATAL_ERROR "Stage 9 disk diagnostics require z80asm")
endif()
set(diagnostic_source_dir "${CMAKE_CURRENT_LIST_DIR}")
set(diagnostic_header "${CMAKE_CURRENT_BINARY_DIR}/disk_diagnostic_image.h")
add_custom_command(
  OUTPUT "${diagnostic_header}"
  COMMAND "${STORAGE_Z80ASM}" -o "${CMAKE_CURRENT_BINARY_DIR}/disk_diagnostic.bin"
    "${diagnostic_source_dir}/disk_diagnostic.asm"
  COMMAND "${CMAKE_COMMAND}"
    "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/disk_diagnostic.bin"
    "-DOUTPUT=${diagnostic_header}" "-DSYMBOL=z80_disk_diagnostic"
    "-DLIMIT=256"
    -P "${diagnostic_source_dir}/../stage06_sram_dma/math_soak_image.cmake"
  DEPENDS "${diagnostic_source_dir}/disk_diagnostic.asm"
    "${diagnostic_source_dir}/../stage06_sram_dma/math_soak_image.cmake"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/disk_diagnostic.bin"
  VERBATIM)
