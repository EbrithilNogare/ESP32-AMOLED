#include "pmu.h"

#define XPOWERS_CHIP_AXP2101
#include <Arduino.h>
#include <XPowersLib.h>
#include <esp_log.h>

#include "board.h"
#include "board_init.h"
#include "events.h"
#include "i2c_bus.h"

namespace core::pmu {

static const char *TAG = "pmu";
static XPowersPMU axp;
static bool ok;
static bool vbus;
static int percent = -1;
static uint16_t millivolts;
static uint32_t lastLevelMs;

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

  // Long-press IRQ after 1 s (register 0x27 bits 5:4 = 00). Written directly:
  // XPowersLib 0.3.3's setIrqLevel() masks the wrong bits (see LESSONS.md).
  int v = i2c::readReg(I2C_ADDR_AXP2101, 0x27);
  if (v >= 0) i2c::writeReg(I2C_ADDR_AXP2101, 0x27, uint8_t(v & ~0x30));

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

  if (vbusEdge) {
    vbus = axp.isVbusIn();
    readLevel();
    events::post({Ev::VbusChanged, vbus ? 1u : 0u, 0});
  }
  if (millis() - lastLevelMs > 30000) readLevel();
  return k;
}

bool onCharger() { return vbus; }
bool charging() { return ok && axp.isCharging(); }
int batteryPercent() { return percent; }
uint16_t batteryMillivolts() { return millivolts; }
bool pwrHeld() { return board::expanderRead(EXIO_PWR_KEY); }

}  // namespace core::pmu
