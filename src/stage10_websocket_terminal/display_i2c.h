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

// Core 1 only. Copies data; the transfer owns the bus until poll returns a result.
bool begin(uint8_t address, const uint8_t *data, size_t length);
Result poll();
Failure failure();
const char *failure_reason(Failure reason);
}

#endif
