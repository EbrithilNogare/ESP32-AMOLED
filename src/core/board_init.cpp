#include "board_init.h"

#include <Arduino.h>
#include <esp_log.h>

#include "board.h"
#include "i2c_bus.h"

namespace core::board {

static const char *TAG = "board";

// TCA9554 registers
static constexpr uint8_t REG_INPUT = 0x00;
static constexpr uint8_t REG_OUTPUT = 0x01;
static constexpr uint8_t REG_CONFIG = 0x03;  // 1 = input

static uint8_t outShadow = 0xFF;
static uint8_t cfgShadow = 0xFF;
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

bool expanderRead(uint8_t pin) {
  int v = i2c::readReg(I2C_ADDR_TCA9554, REG_INPUT);
  return v >= 0 && (v & (1u << pin));
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
  expanderOutput(EXIO_SD_D3, true);
  expanderOutput(EXIO_LCD_TOUCH_RST0, false);
  expanderOutput(EXIO_LCD_TOUCH_RST1, false);
  expanderOutput(EXIO_LCD_TOUCH_RST2, false);
  delay(20);
  expanderOutput(EXIO_LCD_TOUCH_RST0, true);
  expanderOutput(EXIO_LCD_TOUCH_RST1, true);
  expanderOutput(EXIO_LCD_TOUCH_RST2, true);
  delay(60);  // touch controller boot time

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

Revision revision() { return rev; }
const char *revisionName() { return rev == Revision::V2 ? "V2" : "V1"; }
uint8_t touchAddress() { return touchAddr; }
uint16_t lcdColumnOffset() { return rev == Revision::V2 ? 16 : 0; }

}  // namespace core::board
