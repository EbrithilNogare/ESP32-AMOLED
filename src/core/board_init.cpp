#include "board_init.h"

#include <Arduino.h>
#include <esp_log.h>

#include "board.h"
#include "i2c_bus.h"

namespace core::board {

[[maybe_unused]] static const char *TAG = "board";

// TCA9554 registers
static constexpr uint8_t REG_OUTPUT = 0x01;
static constexpr uint8_t REG_CONFIG = 0x03;  // 1 = input

static constexpr uint32_t kResetPulseMs = 20;  // LCD/touch reset LOW time (vendor init)
static constexpr uint32_t kTouchBootMs = 60;   // touch controller boot time after reset

static uint8_t outShadow = 0xFF;
static uint8_t cfgShadow = 0xFF;
enum class Revision : uint8_t { V1, V2 };
static Revision rev = BOARD_REVISION >= 2 ? Revision::V2 : Revision::V1;
static uint8_t touchAddr = 0;

static void expanderOutput(uint8_t pin, bool level) {
  cfgShadow &= ~(1u << pin);
  if (level)
    outShadow |= (1u << pin);
  else
    outShadow &= ~(1u << pin);
  i2c::writeReg(I2C_ADDR_TCA9554, REG_OUTPUT, outShadow);
  i2c::writeReg(I2C_ADDR_TCA9554, REG_CONFIG, cfgShadow);
}

bool begin() {
  if (!i2c::begin()) {
    ESP_LOGE(TAG, "I2C init failed");
    return false;
  }
  if (!i2c::probe(I2C_ADDR_TCA9554)) {
    ESP_LOGE(TAG, "TCA9554 expander not found");
    return false;
  }

  // The panel and touch controller only reset through the expander: an ESP
  // restart (or deep-sleep wake) leaves them in whatever state they were.
  resetPanelAndTouch();

  // Same detection as the vendor BSP: CST820 (V2) or FT3168 (V1).
  if (i2c::probe(I2C_ADDR_TOUCH_CST820)) {
    rev = Revision::V2;
    touchAddr = I2C_ADDR_TOUCH_CST820;
  } else if (i2c::probe(I2C_ADDR_TOUCH_FT3168)) {
    rev = Revision::V1;
    touchAddr = I2C_ADDR_TOUCH_FT3168;
  } else {
    ESP_LOGW(TAG, "no touch controller answered; assuming V%d", BOARD_REVISION);
  }
  ESP_LOGI(TAG, "board %s, touch 0x%02X", revisionName(), touchAddr);
  return true;
}

void resetPanelAndTouch() {
  expanderOutput(EXIO_LCD_TOUCH_RST0, false);
  expanderOutput(EXIO_LCD_TOUCH_RST1, false);
  expanderOutput(EXIO_LCD_TOUCH_RST2, false);
  delay(kResetPulseMs);
  expanderOutput(EXIO_LCD_TOUCH_RST0, true);
  expanderOutput(EXIO_LCD_TOUCH_RST1, true);
  expanderOutput(EXIO_LCD_TOUCH_RST2, true);
  delay(kTouchBootMs);
}

const char *revisionName() { return rev == Revision::V2 ? "V2" : "V1"; }
uint8_t touchAddress() { return touchAddr; }
uint16_t lcdColumnOffset() { return rev == Revision::V2 ? LCD_COL_OFFSET_CO5300 : LCD_COL_OFFSET_SH8601; }

}  // namespace core::board
