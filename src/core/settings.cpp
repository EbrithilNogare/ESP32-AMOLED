#include "settings.h"

#include <string.h>

#include "config.h"
#include "store.h"

namespace core::settings {

struct Wifi {
  char ssid[kSsidMax + 1];
  char password[kPasswordMax + 1];
};

struct Sound {
  uint8_t volume;
  uint8_t muted;
};

static uint8_t brightness = BRIGHTNESS_USER_DEFAULT_PCT;
static Sound sound{SOUND_VOLUME_DEFAULT_PCT, 0};
static Wifi wifi{};

static uint8_t clampPct(int pct) {
  return uint8_t(pct < BRIGHTNESS_USER_MIN_PCT   ? BRIGHTNESS_USER_MIN_PCT
                 : pct > BRIGHTNESS_USER_MAX_PCT ? BRIGHTNESS_USER_MAX_PCT
                                                 : pct);
}

void begin() {
  uint8_t b;
  if (store::load("bright", &b, sizeof b)) brightness = clampPct(b);
  Sound v;
  if (store::load("volume", &v, sizeof v)) sound = {uint8_t(v.volume > 100 ? 100 : v.volume), uint8_t(v.muted != 0)};
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

uint8_t volumePct() { return sound.volume; }
void setVolumePct(uint8_t pct) { sound = {uint8_t(pct > 100 ? 100 : pct), 0}; }
void saveVolume() { store::save("volume", &sound, sizeof sound); }
bool muted() { return sound.muted != 0; }
void setMuted(bool on) {
  sound.muted = on;
  saveVolume();
}

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
