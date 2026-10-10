#include "settings.h"

#include <string.h>

#include "config.h"
#include "store.h"

namespace core::settings {

struct Wifi {
  char ssid[kSsidMax + 1];
  char password[kPasswordMax + 1];
};

static uint8_t brightness = BRIGHTNESS_USER_DEFAULT_PCT;
static Wifi wifi{};

static uint8_t clampPct(int pct) {
  return uint8_t(pct < BRIGHTNESS_USER_MIN_PCT   ? BRIGHTNESS_USER_MIN_PCT
                 : pct > BRIGHTNESS_USER_MAX_PCT ? BRIGHTNESS_USER_MAX_PCT
                                                 : pct);
}

void begin() {
  uint8_t b;
  if (store::load("bright", &b, sizeof b)) brightness = clampPct(b);
  Wifi w;
  if (store::load("wifi", &w, sizeof w)) {
    w.ssid[kSsidMax] = w.password[kPasswordMax] = '\0';  // never trust stored strings
    wifi = w;
  }
}

uint8_t brightnessPct() { return brightness; }
void setBrightnessPct(uint8_t pct) { brightness = clampPct(pct); }
void saveBrightness() { store::save("bright", &brightness, sizeof brightness); }
uint8_t brightnessLevel() { return uint8_t((brightness * 255 + 50) / 100); }

bool hasWifi() { return wifi.ssid[0] != '\0'; }
const char *wifiSsid() { return wifi.ssid; }
const char *wifiPassword() { return wifi.password; }

void setWifi(const char *ssid, const char *password) {
  Wifi w{};
  strncpy(w.ssid, ssid, kSsidMax);
  strncpy(w.password, password ? password : "", kPasswordMax);
  wifi = w;
  store::save("wifi", &wifi, sizeof wifi);
}

}  // namespace core::settings
