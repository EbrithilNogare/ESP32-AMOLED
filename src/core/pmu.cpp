#include "pmu.h"

#define XPOWERS_CHIP_AXP2101
#include <Arduino.h>
#include <XPowersLib.h>
#include <esp_log.h>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"

namespace core::pmu {

[[maybe_unused]] static const char *TAG = "pmu";
static XPowersPMU axp;
static bool ok;
static bool vbus;
static int percent = -1;
static uint16_t millivolts;
static uint32_t lastLevelMs;

// AXP2101 register 0x27 (IRQ level / power-off timing), bits 5:4 = PWR
// long-press time: 00 = 1 s, 01 = 1.5 s, 10 = 2 s, 11 = 2.5 s.
static constexpr uint8_t kRegKeyTiming = 0x27;
static constexpr uint8_t kLongPressMask = 0x30;
static constexpr uint8_t kLongPressShift = 4;
static_assert(BUTTON_LONG_PRESS_MS >= 1000 && BUTTON_LONG_PRESS_MS <= 2500 && BUTTON_LONG_PRESS_MS % 500 == 0,
              "the AXP2101 PWR long press can only be 1000, 1500, 2000 or 2500 ms");
static constexpr uint8_t kLongPressBits = uint8_t(((BUTTON_LONG_PRESS_MS - 1000) / 500) << kLongPressShift);

// XPowersLib register callbacks, routed through our locked bus.
static int regRead(uint8_t dev, uint8_t reg, uint8_t *data, uint8_t len) {
  return i2c::read(dev, reg, data, len) ? 0 : -1;
}
static int regWrite(uint8_t dev, uint8_t reg, uint8_t *data, uint8_t len) {
  return i2c::write(dev, reg, data, len) ? 0 : -1;
}

static void readLevel() {
  if (axp.isBatteryConnect()) {
    percent = axp.getBatteryPercent();
    millivolts = axp.getBattVoltage();
  } else {
    percent = -1;
    millivolts = 0;
  }
  lastLevelMs = millis();
}

bool begin() {
  ok = axp.begin(I2C_ADDR_AXP2101, regRead, regWrite);
  if (!ok) {
    ESP_LOGE(TAG, "AXP2101 not found");
    return false;
  }
  axp.enableBattDetection();
  axp.enableBattVoltageMeasure();
  axp.enableVbusVoltageMeasure();
  axp.enableSystemVoltageMeasure();

  // PWR long-press IRQ after BUTTON_LONG_PRESS_MS, same as BOOT. Written
  // directly: XPowersLib 0.3.3's setIrqLevel() masks the wrong bits (see LESSONS.md).
  int v = i2c::readReg(I2C_ADDR_AXP2101, kRegKeyTiming);
  if (v >= 0) i2c::writeReg(I2C_ADDR_AXP2101, kRegKeyTiming, uint8_t((v & ~kLongPressMask) | kLongPressBits));

  axp.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  axp.clearIrqStatus();
  axp.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ | XPOWERS_AXP2101_VBUS_INSERT_IRQ |
                XPOWERS_AXP2101_VBUS_REMOVE_IRQ);

  vbus = axp.isVbusIn();
  readLevel();
  ESP_LOGI(TAG, "battery %d %% (%u mV), %s", percent, millivolts, vbus ? "on USB" : "on battery");
  return true;
}

KeyEvents poll() {
  KeyEvents k{false, false};
  if (!ok) return k;
  axp.getIrqStatus();
  k.shortPress = axp.isPekeyShortPressIrq();
  k.longPress = axp.isPekeyLongPressIrq();
  const bool vbusEdge = axp.isVbusInsertIrq() || axp.isVbusRemoveIrq();
  axp.clearIrqStatus();

  if (vbusEdge) {  // the power manager picks the change up through onCharger()
    vbus = axp.isVbusIn();
    readLevel();
  }
  if (millis() - lastLevelMs > BATTERY_REFRESH_MS) readLevel();
  return k;
}

bool onCharger() { return vbus; }
bool charging() { return ok && axp.isCharging(); }
int batteryPercent() { return percent; }
uint16_t batteryMillivolts() { return millivolts; }

}  // namespace core::pmu
