#include "rtc_clock.h"

#include <Arduino.h>
#include <esp_log.h>
#include <sys/time.h>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"

namespace core::clock {

static const char *TAG = "clock";
static bool timeValid;

// PCF85063 time registers 0x04..0x0A: seconds (bit 7 = oscillator stopped),
// minutes, hours, days, weekdays, months, years — all BCD, 24 h mode.
static constexpr uint8_t REG_SECONDS = 0x04;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

bool begin() {
  setenv("TZ", TIMEZONE, 1);
  tzset();

  uint8_t r[7];
  if (!i2c::read(I2C_ADDR_PCF85063, REG_SECONDS, r, sizeof r)) {
    ESP_LOGW(TAG, "RTC not found");
    return false;
  }
  if (r[0] & 0x80) {
    ESP_LOGW(TAG, "RTC lost power; waiting for SNTP");
    return true;
  }
  struct tm t = {};
  t.tm_sec = bcd2bin(r[0] & 0x7F);
  t.tm_min = bcd2bin(r[1] & 0x7F);
  t.tm_hour = bcd2bin(r[2] & 0x3F);
  t.tm_mday = bcd2bin(r[3] & 0x3F);
  t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
  t.tm_year = bcd2bin(r[6]) + 100;  // RTC holds 2000-2099
  // The RTC stores UTC; mktime() would apply TZ, so convert by hand.
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t utc = mktime(&t);
  setenv("TZ", TIMEZONE, 1);
  tzset();
  timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  timeValid = true;
  return true;
}

bool valid() { return timeValid; }

void onNtpSynced() {
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  const uint8_t r[7] = {
      bin2bcd(t.tm_sec),
      bin2bcd(t.tm_min),
      bin2bcd(t.tm_hour),
      bin2bcd(t.tm_mday),
      uint8_t(t.tm_wday),
      bin2bcd(t.tm_mon + 1),
      bin2bcd(uint8_t(t.tm_year - 100)),
  };
  if (i2c::write(I2C_ADDR_PCF85063, REG_SECONDS, r, sizeof r)) timeValid = true;
  ESP_LOGI(TAG, "RTC set from SNTP");
}

}  // namespace core::clock
