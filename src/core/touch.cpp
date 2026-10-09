#include "touch.h"

#include <Arduino.h>
#include <esp_log.h>

#include "board.h"
#include "i2c_bus.h"

namespace core::touch {

static const char *TAG = "touch";

static uint8_t addr;
static bool (*pressHook)();
static void (*activityHook)();
static lv_indev_t *dev;
static volatile bool intFlag;
static bool down;
static bool swallowing;
static int16_t lastX, lastY;

static void IRAM_ATTR onInt() { intFlag = true; }

static void readCb(lv_indev_t *, lv_indev_data_t *data) {
  // Read on a fresh interrupt, and keep reading while a finger is down so we
  // see the release even if the controller stops pulsing INT.
  if (addr && (intFlag || down)) {
    intFlag = false;
    uint8_t b[7];
    bool pressed = false;
    if (i2c::read(addr, 0x00, b, sizeof b)) {
      const uint8_t points = b[2] & 0x0F;
      if (points >= 1 && points <= 5) {
        lastX = int16_t(((b[3] & 0x0F) << 8) | b[4]);
        lastY = int16_t(((b[5] & 0x0F) << 8) | b[6]);
        if (lastX >= LCD_WIDTH) lastX = LCD_WIDTH - 1;
        if (lastY >= LCD_HEIGHT) lastY = LCD_HEIGHT - 1;
        pressed = true;
      }
    }
    if (pressed && !down) swallowing = pressHook && !pressHook();
    if (pressed && activityHook) activityHook();
    if (!pressed) swallowing = false;
    down = pressed;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = (down && !swallowing) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool begin(uint8_t i2cAddress, bool (*onPress)(), void (*onActivity)()) {
  addr = i2cAddress;
  pressHook = onPress;
  activityHook = onActivity;
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), onInt, FALLING);
  dev = lv_indev_create();
  lv_indev_set_type(dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(dev, readCb);
  if (!addr) ESP_LOGW(TAG, "no touch controller: input from buttons only");
  return addr != 0;
}

lv_indev_t *indev() { return dev; }

void kick() { intFlag = true; }

void sleep() {
  if (!addr) return;
  // CST8xx: 0xE5 = 0x03; FocalTech: power mode 0xA5 = 0x03 (hibernate).
  if (addr == I2C_ADDR_TOUCH_CST820)
    i2c::writeReg(addr, 0xE5, 0x03);
  else
    i2c::writeReg(addr, 0xA5, 0x03);
}

}  // namespace core::touch
