#ifndef Z80_DISPLAY_I2C_H
#define Z80_DISPLAY_I2C_H

#include <cstddef>
#include <cstdint>

namespace display_i2c {
constexpr unsigned PIN_SCL = 26;
constexpr unsigned PIN_SDA = 22;
// One OLED page: three Co=1 command pairs, a data control byte, 128 pixels.
constexpr size_t MAX_DATA_BYTES = 135;
enum class Result : uint8_t { pending, complete, nack, failed };
enum class Failure : uint8_t { none, program, state_machine, dma, timeout };

// Each transfer picks its rate. The OLED and the ATtiny85 speaker both use
// Fast-mode; Standard-mode is the default.
enum class Speed : uint8_t { standard, fast };
constexpr uint32_t STANDARD_BAUD = 100000;
constexpr uint32_t FAST_BAUD = 400000;

// Core 0 only. Copies data; the transfer owns the bus until poll returns a result.
bool begin(uint8_t address, const uint8_t *data, size_t length,
           Speed speed = Speed::standard);
bool begin_read(uint8_t address, uint8_t *data, size_t length,
                Speed speed = Speed::standard);
bool busy();
Result poll();
Failure failure();
const char *failure_reason(Failure reason);
}

#endif
