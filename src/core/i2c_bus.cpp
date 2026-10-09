#include "i2c_bus.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board.h"

namespace core::i2c {

static SemaphoreHandle_t mutex;

Lock::Lock() { xSemaphoreTakeRecursive(mutex, portMAX_DELAY); }
Lock::~Lock() { xSemaphoreGiveRecursive(mutex); }

bool begin() {
  if (!mutex) mutex = xSemaphoreCreateRecursiveMutex();
  Lock lock;
  bool ok = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(20);
  return ok;
}

bool probe(uint8_t addr) {
  Lock lock;
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len) {
  Lock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (len) Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) { return write(addr, reg, &value, 1); }

bool read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
  Lock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, len) != len) return false;
  for (size_t i = 0; i < len; i++) data[i] = Wire.read();
  return true;
}

int readReg(uint8_t addr, uint8_t reg) {
  uint8_t v;
  return read(addr, reg, &v, 1) ? v : -1;
}

}  // namespace core::i2c
