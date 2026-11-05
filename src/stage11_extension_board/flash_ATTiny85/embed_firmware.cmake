file(SIZE "${INPUT}" firmware_size)
if(firmware_size LESS 2 OR firmware_size GREATER 8192)
  message(FATAL_ERROR "ATtiny85 image must contain 2..8192 bytes")
endif()
file(READ "${INPUT}" firmware HEX)
string(REGEX REPLACE "(..)" "0x\\1," firmware "${firmware}")
file(WRITE "${OUTPUT}"
  "#ifndef ATTINY85_FIRMWARE_H\n#define ATTINY85_FIRMWARE_H\n"
  "#include <stdint.h>\n"
  "static const uint8_t attiny85_firmware[] = {${firmware}};\n"
  "#endif\n")
