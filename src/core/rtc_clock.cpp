#include "rtc_clock.h"

#include <Arduino.h>
#include <esp_log.h>
#include <sys/time.h>

#include "board.h"
#include "config.h"
#include "i2c_bus.h"
#include "store.h"
#include "ticks.h"

namespace core::clock {

[[maybe_unused]] static const char *TAG = "clock";
static bool timeValid;
static int64_t monoOffset;                     // mono() - extend(): the time before the last deep sleep
static constexpr int64_t kKeepDriftMs = 1500;  // after a deep sleep: keep the ESP time within this of the RTC

// PCF85063 time registers 0x04..0x0A: seconds (bit 7 = oscillator stopped),
// minutes, hours, days, weekdays, months, years — all BCD, 24 h mode.
static constexpr uint8_t REG_SECONDS = 0x04;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

int64_t wallMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

bool begin(bool afterDeepSleep) {
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
  timeValid = true;
  // The RTC counts whole seconds: utc + 0.5 s is the best guess of "now".
  const int64_t rtcMs = int64_t(utc) * 1000 + 500;
  const int64_t drift = wallMs() - rtcMs;
  if (afterDeepSleep && drift > -kKeepDriftMs && drift < kKeepDriftMs) return true;
  if (afterDeepSleep) ESP_LOGI(TAG, "ESP clock drifted %lld ms in deep sleep; RTC time taken", (long long)drift);
  timeval tv = {utc, 500000};
  settimeofday(&tv, nullptr);
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
  if (i2c::write(I2C_ADDR_PCF85063, REG_SECONDS, r, sizeof r)) {
    timeValid = true;
    ESP_LOGI(TAG, "RTC set from SNTP");
  } else {
    ESP_LOGW(TAG, "RTC write failed");
  }
}

void noteSleep(uint64_t monoNow) {
  store::rtc().monoAtSleep = monoNow;
  store::rtc().wallAtSleepMs = wallMs();
}

void resume() {
  const store::RtcState &r = store::rtc();
  if (!store::rtcValid() || !r.monoAtSleep) return;
  int64_t slept = wallMs() - r.wallAtSleepMs;
  if (slept < 0) slept = 0;  // the clock was set backwards meanwhile
  monoOffset = int64_t(r.monoAtSleep) + slept - int64_t(extend(millis()));
  ESP_LOGI(TAG, "asleep %lld s", (long long)(slept / 1000));
}

}  // namespace core::clock

namespace core {
uint64_t mono(uint32_t ms) { return uint64_t(int64_t(extend(ms)) + clock::monoOffset); }
}  // namespace core
