// QMI8658 register setup checked against lewisxhe/SensorLib (MIT,
// src/sensor/imu/qmi8658): WHO_AM_I 0x00 = 0x05, CTRL1 bit 6 = address
// auto-increment, CTRL2 = range << 4 | ODR, CTRL5 bit 0 = accel LPF,
// CTRL7 bit 0 = accel enable, data little-endian from AX_L 0x35, reset 0x60 = 0xB0.
#include "imu.h"

#include <Arduino.h>
#include <esp_log.h>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"

namespace core::imu {

[[maybe_unused]] static const char *TAG = "imu";

static constexpr uint8_t REG_WHO_AM_I = 0x00, REG_CTRL1 = 0x02, REG_CTRL2 = 0x03, REG_CTRL5 = 0x06, REG_CTRL7 = 0x08,
                         REG_AX_L = 0x35, REG_RST_RESULT = 0x4D, REG_RESET = 0x60;
static constexpr uint8_t kWhoAmI = 0x05;
static constexpr uint8_t kCtrl2 = (0 << 4) | 0x06;  // ±2 g, aODR 6: ≈ 112–125 Hz (depends on mode)
static constexpr uint8_t kCtrl5 = (0x01 << 1) | 1;  // accel LPF on, 3.63 % of ODR
static constexpr float kLsbPerG = 16384.0f;         // ±2 g

static bool ok;

bool begin() {
  if (i2c::readReg(I2C_ADDR_QMI8658, REG_WHO_AM_I) != kWhoAmI) {
    ESP_LOGW(TAG, "QMI8658 not found");
    return ok = false;
  }
  i2c::writeReg(I2C_ADDR_QMI8658, REG_RESET, 0xB0);
  for (int i = 0; i < 50; i++) {  // up to 500 ms
    delay(10);
    const int r = i2c::readReg(I2C_ADDR_QMI8658, REG_RST_RESULT);
    if (r >= 0 && r != 0xFF && (r & 0x80)) break;
  }
  ok = i2c::writeReg(I2C_ADDR_QMI8658, REG_CTRL1, 0x40) &&  // address auto-increment
       i2c::writeReg(I2C_ADDR_QMI8658, REG_CTRL2, kCtrl2) && i2c::writeReg(I2C_ADDR_QMI8658, REG_CTRL5, kCtrl5) &&
       i2c::writeReg(I2C_ADDR_QMI8658, REG_CTRL7, 0x00);  // standby until an app needs it
  ESP_LOGI(TAG, "QMI8658 %s", ok ? "ready" : "config failed");
  return ok;
}

bool present() { return ok; }

void enable(bool on) {
  if (ok) i2c::writeReg(I2C_ADDR_QMI8658, REG_CTRL7, on ? 0x01 : 0x00);
}

bool read(level::Accel &out) {
  uint8_t b[6];
  if (!ok || !i2c::read(I2C_ADDR_QMI8658, REG_AX_L, b, sizeof b)) return false;
  const float raw[3] = {int16_t(b[1] << 8 | b[0]) / kLsbPerG, int16_t(b[3] << 8 | b[2]) / kLsbPerG,
                        int16_t(b[5] << 8 | b[4]) / kLsbPerG};
  static uint32_t lastLogMs;
  if (millis() - lastLogMs >= 1000) {  // for checking the axis mapping (config.h)
    lastLogMs = millis();
    ESP_LOGD(TAG, "raw %+.2f %+.2f %+.2f g", raw[0], raw[1], raw[2]);
  }
  out = {raw[IMU_SCREEN_X_AXIS] * IMU_SCREEN_X_SIGN, raw[IMU_SCREEN_Y_AXIS] * IMU_SCREEN_Y_SIGN,
         raw[IMU_SCREEN_Z_AXIS] * IMU_SCREEN_Z_SIGN};
  return true;
}

}  // namespace core::imu
