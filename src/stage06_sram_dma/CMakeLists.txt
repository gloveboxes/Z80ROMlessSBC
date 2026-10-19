add_z80_stage(z80_stage06_sram_loader main.c z80_cpu)
add_z80_stage(z80_stage06_bus_grant bus_grant_test.c z80_cpu)
add_z80_stage(z80_stage06_step_loader step_loader.c z80_cpu)
target_sources(z80_stage06_step_loader PRIVATE full_memory_test.c)
set_source_files_properties(full_memory_test.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
add_z80_stage(z80_stage06_pwm_memory pwm_memory_test.c z80_io_trap)

# Compile the entire clock/CPU/trap chain separately; normal stages stay at 8 MHz.
add_library(z80_pwm_experimental STATIC
  ../common/clock.c
  ../common/sram.c
  ../common/cpu.c
  ../common/io_trap.c
)
target_compile_definitions(z80_pwm_experimental PUBLIC Z80_EXPERIMENTAL_12MHZ=1)
target_compile_options(z80_pwm_experimental PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_pwm_experimental PUBLIC
  z80_bus hardware_pwm hardware_clocks hardware_watchdog)
add_z80_stage(z80_stage06_pwm_memory_experimental pwm_memory_test.c
  z80_pwm_experimental)

get_target_property(pwm_experimental_sources z80_pwm_experimental SOURCES)
add_library(z80_pwm_14mhz STATIC ${pwm_experimental_sources})
target_compile_definitions(z80_pwm_14mhz PUBLIC Z80_EXPERIMENTAL_14MHZ=1)
target_compile_options(z80_pwm_14mhz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_pwm_14mhz PUBLIC
  z80_bus hardware_pwm hardware_clocks hardware_watchdog)
add_z80_stage(z80_stage06_pwm_memory_14mhz pwm_memory_test.c z80_pwm_14mhz)

add_library(z80_pwm_15mhz STATIC ${pwm_experimental_sources})
target_compile_definitions(z80_pwm_15mhz PUBLIC Z80_EXPERIMENTAL_15MHZ=1)
target_compile_options(z80_pwm_15mhz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_pwm_15mhz PUBLIC
  z80_bus hardware_pwm hardware_clocks hardware_watchdog)
add_z80_stage(z80_stage06_pwm_memory_15mhz pwm_memory_test.c z80_pwm_15mhz)

add_library(z80_pwm_16mhz STATIC ${pwm_experimental_sources})
target_compile_definitions(z80_pwm_16mhz PUBLIC Z80_EXPERIMENTAL_16MHZ=1)
target_compile_options(z80_pwm_16mhz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(z80_pwm_16mhz PUBLIC
  z80_bus hardware_pwm hardware_clocks hardware_watchdog)
add_z80_stage(z80_stage06_pwm_memory_16mhz pwm_memory_test.c z80_pwm_16mhz)

foreach(screen_mhz IN ITEMS 17 18 19 20)
  add_library(z80_pwm_${screen_mhz}mhz STATIC ${pwm_experimental_sources})
  target_compile_definitions(z80_pwm_${screen_mhz}mhz PUBLIC
    Z80_EXPERIMENTAL_${screen_mhz}MHZ=1)
  target_compile_options(z80_pwm_${screen_mhz}mhz PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(z80_pwm_${screen_mhz}mhz PUBLIC
    z80_bus hardware_pwm hardware_clocks hardware_watchdog)
  add_z80_stage(z80_stage06_pwm_memory_${screen_mhz}mhz pwm_memory_test.c
    z80_pwm_${screen_mhz}mhz)
endforeach()

find_program(SOAK_Z80ASM z80asm)
if(SOAK_Z80ASM)
  include(math_soak_build.cmake)
  add_z80_stage(z80_stage06_math_soak_20mhz math_soak_test.c z80_pwm_20mhz)
  target_sources(z80_stage06_math_soak_20mhz PRIVATE "${soak_header}")
  target_include_directories(z80_stage06_math_soak_20mhz PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
  add_z80_stage(z80_stage06_math_soak_14h_20mhz math_soak_test.c z80_pwm_20mhz)
  target_compile_definitions(z80_stage06_math_soak_14h_20mhz PRIVATE Z80_SOAK_14H=1)
  target_sources(z80_stage06_math_soak_14h_20mhz PRIVATE "${soak_header}")
  target_include_directories(z80_stage06_math_soak_14h_20mhz PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
  add_z80_stage(z80_stage06_scope_20mhz scope_test.c z80_pwm_20mhz)
  target_sources(z80_stage06_scope_20mhz PRIVATE "${scope_header}")
  target_include_directories(z80_stage06_scope_20mhz PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
else()
  message(STATUS "z80asm not found; 20 MHz math soak and scope targets unavailable")
endif()