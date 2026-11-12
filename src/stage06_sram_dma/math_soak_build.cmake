include("${CMAKE_CURRENT_LIST_DIR}/../common/z80_assembler.cmake")
if(NOT Z80_ASSEMBLER_AVAILABLE)
  message(FATAL_ERROR "The math soak target requires z80asm")
endif()
set(soak_source_dir "${CMAKE_CURRENT_LIST_DIR}")
function(build_soak_image name symbol limit header_variable)
  set(header "${CMAKE_CURRENT_BINARY_DIR}/${name}_image.h")
  add_custom_command(
    OUTPUT "${header}"
    COMMAND ${Z80_ASSEMBLER} -I "${soak_source_dir}"
      -o "${CMAKE_CURRENT_BINARY_DIR}/${name}.bin" "${soak_source_dir}/${name}.asm"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}.bin" "-DOUTPUT=${header}"
      "-DSYMBOL=${symbol}" "-DLIMIT=${limit}"
      -P "${soak_source_dir}/math_soak_image.cmake"
    DEPENDS "${soak_source_dir}/${name}.asm" "${soak_source_dir}/math_workload.asm"
      "${soak_source_dir}/math_soak_image.cmake" ${Z80_ASSEMBLER_DEPENDENCIES}
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${name}.bin"
    VERBATIM)
  set(${header_variable} "${header}" PARENT_SCOPE)
endfunction()
build_soak_image(math_soak z80_math_soak_code 4096 soak_header)
build_soak_image(scope_test z80_scope_code 3840 scope_header)
