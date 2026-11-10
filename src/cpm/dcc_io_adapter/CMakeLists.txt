cmake_minimum_required(VERSION 3.16)
project(dcc_debug_io_adapter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL QUIET)
find_package(Threads REQUIRED)

set(DEFAULT_ENV_FILE ${CMAKE_CURRENT_BINARY_DIR}/altair_env.txt)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/altair_env.txt)
    set(DEFAULT_ENV_SOURCE assets/altair_env.txt)
else()
    set(DEFAULT_ENV_SOURCE assets/altair_env.example.txt)
endif()
configure_file(${DEFAULT_ENV_SOURCE} ${DEFAULT_ENV_FILE} COPYONLY)

set(ADAPTER_SOURCES
    src/dcc_debug_io_adapter.c
    src/dcc_debug_terminal_input.c
    src/sbc_disk.c
    host/PortDrivers/io_ports.c
    host/PortDrivers/host_chat_io.c
    host/PortDrivers/environment_io.c
    host/PortDrivers/host_files_io.c
    host/PortDrivers/host_http_io.c
    ../../common/http_io.c
    ../../common/http_stream.c
    ../../common/io_ports.c
    host/PortDrivers/time_io.c
    host/PortDrivers/utility_io.c
    host/PortDrivers/weather_io.c
    port_drivers/json_scan.c
    port_drivers/interrupt_timer.c
)

# Port 0x15 speaker: the Stage 11 ATtiny85 firmware engine plus miniaudio.
option(Z80SBC_SPEAKER_AUDIO "Play the port 0x15 speaker through miniaudio" ON)
set(MINIAUDIO_HEADER "" CACHE FILEPATH "Local miniaudio.h; empty downloads the pinned release")
set(STAGE11_TARGET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../stage11_extension_board/flash_ATTiny85)
set(STAGE11_AVR_MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../stage11_extension_board/tests/fakes)

add_library(z80sbc-speaker OBJECT
    host/PortDrivers/speaker_io.c
    host/PortDrivers/speaker_audio.c
)
target_include_directories(z80sbc-speaker PRIVATE host)
set_source_files_properties(host/PortDrivers/speaker_io.c PROPERTIES
    INCLUDE_DIRECTORIES "${STAGE11_TARGET_DIR};${STAGE11_AVR_MODEL_DIR}")
set_target_properties(z80sbc-speaker PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)
if(Z80SBC_SPEAKER_AUDIO)
    if(NOT MINIAUDIO_HEADER)
        set(MINIAUDIO_HEADER ${CMAKE_CURRENT_BINARY_DIR}/miniaudio/miniaudio.h)
        file(DOWNLOAD
            https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/miniaudio.h
            ${MINIAUDIO_HEADER}
            EXPECTED_HASH SHA256=6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e
            STATUS MINIAUDIO_STATUS)
        list(GET MINIAUDIO_STATUS 0 MINIAUDIO_ERROR)
        if(MINIAUDIO_ERROR)
            file(REMOVE ${MINIAUDIO_HEADER})
            message(FATAL_ERROR "miniaudio download failed: ${MINIAUDIO_STATUS}. "
                "Set MINIAUDIO_HEADER or -DZ80SBC_SPEAKER_AUDIO=OFF")
        endif()
    endif()
    get_filename_component(MINIAUDIO_DIR ${MINIAUDIO_HEADER} DIRECTORY)
    target_include_directories(z80sbc-speaker PRIVATE ${MINIAUDIO_DIR})
    target_compile_definitions(z80sbc-speaker PRIVATE
        Z80SBC_SPEAKER_AUDIO=1
        MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
        MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE
    )
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(host/PortDrivers/speaker_audio.c
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()
    if(UNIX AND NOT APPLE)
        set(SPEAKER_LIBRARIES ${CMAKE_DL_LIBS} m)
    endif()
endif()

add_library(dcc-debug-io-adapter SHARED ${ADAPTER_SOURCES}
    $<TARGET_OBJECTS:z80sbc-speaker>)
target_include_directories(dcc-debug-io-adapter PRIVATE
    include
    host
    host/PortDrivers
    port_drivers
    ../../common
    ../../common/include
)
target_compile_definitions(dcc-debug-io-adapter PRIVATE
    DCC_DEBUG_IO_ADAPTER_BUILD=1
)
target_link_libraries(dcc-debug-io-adapter PRIVATE Threads::Threads ${SPEAKER_LIBRARIES})
set_target_properties(dcc-debug-io-adapter PROPERTIES
    C_VISIBILITY_PRESET hidden
    OUTPUT_NAME z80sbc-io-adapter
)

if(CURL_FOUND AND TARGET CURL::libcurl)
    set(Z80_HTTP_CA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../stage10_websocket_terminal/http_ca.pem"
        CACHE FILEPATH "PEM trust bundle for native HTTPS file transfer")
    target_compile_definitions(dcc-debug-io-adapter PRIVATE HAVE_LIBCURL=1)
    target_compile_definitions(dcc-debug-io-adapter PRIVATE
        Z80_HTTP_CA_FILE="${Z80_HTTP_CA_FILE}")
    target_link_libraries(dcc-debug-io-adapter PRIVATE CURL::libcurl)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dcc-debug-io-adapter PRIVATE
        -Wall
        -Wextra
        -Wno-missing-field-initializers
    )
    target_compile_options(z80sbc-speaker PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(dcc-debug-io-adapter PRIVATE /W4)
    target_compile_definitions(dcc-debug-io-adapter PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(io-adapter-test tests/io_adapter_test.c)
    target_include_directories(io-adapter-test PRIVATE include)
    target_link_libraries(io-adapter-test PRIVATE dcc-debug-io-adapter)
    add_test(NAME io-adapter-lifecycle COMMAND io-adapter-test
             ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(speaker-io-test tests/speaker_io_test.c
        $<TARGET_OBJECTS:z80sbc-speaker>)
    target_include_directories(speaker-io-test PRIVATE host ${STAGE11_TARGET_DIR})
    target_link_libraries(speaker-io-test PRIVATE Threads::Threads ${SPEAKER_LIBRARIES})
    add_test(NAME speaker-io COMMAND speaker-io-test)
endif()
