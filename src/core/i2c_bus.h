#pragma once
#include <stddef.h>
#include <stdint.h>

// The one shared I2C bus (touch, PMU, RTC, IMU, codec, expander). Every access
// goes through these helpers, which hold a recursive mutex for the duration
// of a transaction, so other tasks can't interleave on the bus.
namespace core::i2c {

bool begin();
bool probe(uint8_t addr);
bool write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
bool writeReg(uint8_t addr, uint8_t reg, uint8_t value);
bool read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
int readReg(uint8_t addr, uint8_t reg);  // -1 on error

// For code that talks to Wire directly (rare): hold the bus while doing it.
class Lock {
 public:
  Lock();
  ~Lock();
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

}  // namespace core::i2c
