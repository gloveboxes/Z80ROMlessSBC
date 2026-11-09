cmake_minimum_required(VERSION 3.16)
project(z80_debug_host C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(Z80_DEBUG_HOST_BUILD_EXAMPLES "Build debugger I/O adapter examples" ON)

find_package(Threads REQUIRED)

set(HARDWARE_DIR ${CMAKE_CURRENT_LIST_DIR}/hardware)
set(ASSETS_DIR ${CMAKE_CURRENT_LIST_DIR}/assets)
set(IO_ADAPTER_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
get_filename_component(DEBUGGER_ROOT ${CMAKE_CURRENT_LIST_DIR} REALPATH)

function(assert_debugger_local_path PATH_TO_CHECK)
    get_filename_component(RESOLVED_PATH ${PATH_TO_CHECK} REALPATH
                           BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    string(FIND "${RESOLVED_PATH}" "${DEBUGGER_ROOT}/" ROOT_PREFIX_POSITION)
    if(NOT ROOT_PREFIX_POSITION EQUAL 0)
        message(FATAL_ERROR
            "z80_debug_host target path escapes the standalone project: ${RESOLVED_PATH}")
    endif()
endfunction()

foreach(REQUIRED_STANDALONE_PATH
        ${HARDWARE_DIR}/memory.c
    ${IO_ADAPTER_INCLUDE_DIR}/dcc_debug_io_adapter.h
        ${ASSETS_DIR}/disks/cpm63k.dsk
        ${ASSETS_DIR}/disks/disk_c_blank.dsk
        ${ASSETS_DIR}/disks/disk_d_blank.dsk)
    if(NOT EXISTS ${REQUIRED_STANDALONE_PATH})
        message(FATAL_ERROR "z80_debug_host standalone asset is missing: ${REQUIRED_STANDALONE_PATH}")
    endif()
endforeach()

file(READ ${CMAKE_CURRENT_LIST_DIR}/x80.cxx DEBUGGER_X80_SOURCE)
file(READ ${CMAKE_CURRENT_LIST_DIR}/x80.hxx DEBUGGER_X80_HEADER)
foreach(FORBIDDEN_Z80_TOKEN "i8080_" "fZ80Mode" "Z80Mode")
    string(FIND "${DEBUGGER_X80_SOURCE}${DEBUGGER_X80_HEADER}"
           "${FORBIDDEN_Z80_TOKEN}" FORBIDDEN_Z80_POSITION)
    if(NOT FORBIDDEN_Z80_POSITION EQUAL -1)
        message(FATAL_ERROR
            "z80_debug_host Z80 engine contains forbidden 8080-mode token: ${FORBIDDEN_Z80_TOKEN}")
    endif()
endforeach()
unset(DEBUGGER_X80_SOURCE)
unset(DEBUGGER_X80_HEADER)

set(DEBUGGER_HOST_SOURCES
    dcc_host.cpp
    dcc_host_full_cpm.cpp
    dcc_host_fixture_publish.cpp
    directory_disk.cpp
    dcc_host_debug_metadata.cpp
    dcc_host_debug_evaluator.cpp
    dcc_host_io_adapter_loader.cpp
    dcc_host_target_terminal.cpp
    dcc_host_mi_server.cpp
    ${HARDWARE_DIR}/interrupt_controller.c
    ${HARDWARE_DIR}/universal_88dcdd.c
    ${HARDWARE_DIR}/memory.c
    sbc_bank.c
    x80.cxx
    cpu_x80_adapter.cpp
)

foreach(DEBUGGER_SOURCE ${DEBUGGER_HOST_SOURCES})
    assert_debugger_local_path(${DEBUGGER_SOURCE})
endforeach()

file(GLOB DEBUGGER_ROOT_CODE
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.h
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/*.hpp
    ${CMAKE_CURRENT_LIST_DIR}/*.cxx
    ${CMAKE_CURRENT_LIST_DIR}/*.hxx)
file(GLOB_RECURSE DEBUGGER_LOCAL_CODE
    ${HARDWARE_DIR}/*.c ${HARDWARE_DIR}/*.h
    ${HARDWARE_DIR}/*.cxx ${HARDWARE_DIR}/*.hxx)
foreach(DEBUGGER_SOURCE ${DEBUGGER_ROOT_CODE} ${DEBUGGER_LOCAL_CODE})
    file(STRINGS ${DEBUGGER_SOURCE} PARENT_INCLUDE
         REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]\\.\\./")
    if(PARENT_INCLUDE)
        message(FATAL_ERROR
            "z80_debug_host source has a parent-directory include: ${DEBUGGER_SOURCE}")
    endif()
endforeach()

add_executable(z80-debug-host ${DEBUGGER_HOST_SOURCES})

target_include_directories(z80-debug-host PRIVATE
    .
    ${IO_ADAPTER_INCLUDE_DIR}
    ${HARDWARE_DIR}
    ${HARDWARE_DIR}/x80_compat
)

foreach(DEBUGGER_INCLUDE_DIR
    ${IO_ADAPTER_INCLUDE_DIR}
        ${HARDWARE_DIR}
        ${HARDWARE_DIR}/x80_compat)
    assert_debugger_local_path(${DEBUGGER_INCLUDE_DIR})
endforeach()

target_compile_definitions(z80-debug-host PRIVATE
    Z80_DEBUG_HOST_ASSETS_DIR="${ASSETS_DIR}"
    TRACK_Z80_R_REGISTER=1
    TRACK_Z80_MEMPTR=1
)

target_link_libraries(z80-debug-host PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(WIN32)
    target_link_libraries(z80-debug-host PRIVATE ws2_32)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(z80-debug-host PRIVATE
        -Wall
        -Wextra
        -Wno-missing-field-initializers
    )
elseif(MSVC)
    target_compile_options(z80-debug-host PRIVATE /W4 /wd4201)
    target_compile_definitions(z80-debug-host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        x80.cxx
        cpu_x80_adapter.cpp
        sbc_emulator.cpp
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

# The test-facing emulator library: the same x80 engine and bank model as the
# host, driven through the C API in sbc_emulator.h.
add_library(z80sbc-emulator SHARED
    sbc_emulator.cpp
    sbc_bank.c
    ${HARDWARE_DIR}/memory.c
    x80.cxx
)
target_include_directories(z80sbc-emulator PRIVATE
    .
    ${HARDWARE_DIR}
    ${HARDWARE_DIR}/x80_compat
)
target_compile_definitions(z80sbc-emulator PRIVATE
    TRACK_Z80_R_REGISTER=1
    TRACK_Z80_MEMPTR=1
)


if(MSVC)
    set_source_files_properties(
        ${HARDWARE_DIR}/interrupt_controller.c
        PROPERTIES COMPILE_OPTIONS "/experimental:c11atomics"
    )
endif()

include(CTest)

function(dcc_enable_test_assertions TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -UNDEBUG)
    endif()
endfunction()

if(Z80_DEBUG_HOST_BUILD_EXAMPLES)
    add_subdirectory(examples/io_adapter)
endif()
