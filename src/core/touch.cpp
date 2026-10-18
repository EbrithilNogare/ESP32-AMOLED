#include "touch.h"

#include <Arduino.h>
#include <esp_log.h>
#include <lvgl.h>

#include "board.h"
#include "i2c_bus.h"

namespace core::touch {

[[maybe_unused]] static const char *TAG = "touch";

static uint8_t addr;
static bool (*pressHook)();
static void (*activityHook)();
static volatile bool intFlag;
static bool down;
static bool swallowing;
static int16_t lastX, lastY;
static touchcal::Cal cal;
static bool raw;

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
        if (!raw) {
          lastX = touchcal::mapX(cal, lastX);
          lastY = touchcal::mapY(cal, lastY);
        }
        lastX = lastX < 0 ? 0 : lastX >= LCD_WIDTH ? LCD_WIDTH - 1 : lastX;
        lastY = lastY < 0 ? 0 : lastY >= LCD_HEIGHT ? LCD_HEIGHT - 1 : lastY;
        pressed = true;
      }
    }
    if (pressed && !down) {
      ESP_LOGD(TAG, "down %d,%d%s", lastX, lastY, raw ? " (raw)" : "");
      swallowing = pressHook && !pressHook();
    }
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
  lv_indev_t *dev = lv_indev_create();
  lv_indev_set_type(dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(dev, readCb);
  if (!addr) ESP_LOGW(TAG, "no touch controller: input from buttons only");
  return addr != 0;
}

void kick() { intFlag = true; }
void wake() {
  down = swallowing = false;
  intFlag = false;
}
void setCalibration(const touchcal::Cal &c) { cal = c; }
void setRaw(bool on) { raw = on; }

void sleep() {
  if (!addr) return;
  // CST8xx: 0xE5 = 0x03; FocalTech: power mode 0xA5 = 0x03 (hibernate).
  if (addr == I2C_ADDR_TOUCH_CST820)
    i2c::writeReg(addr, 0xE5, 0x03);
  else
    i2c::writeReg(addr, 0xA5, 0x03);
}

}  // namespace core::touch
