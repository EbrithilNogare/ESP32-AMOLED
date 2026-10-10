#pragma once
#include <stddef.h>
#include <stdint.h>

// User settings changed on the device (quick settings, Wi-Fi screen), kept in
// NVS so they survive reboots, deep sleep and firmware updates (USB or OTA
// rewrite only the app partition). Only "erase flash" clears them.
// Saved Wi-Fi credentials take precedence over WIFI_SSID in secrets.h.
namespace core::settings {

constexpr size_t kSsidMax = 32;      // 802.11 limit
constexpr size_t kPasswordMax = 63;  // WPA2 passphrase limit

void begin();  // after store::begin()

// Screen brightness for the active screen, BRIGHTNESS_USER_MIN_PCT..MAX_PCT.
uint8_t brightnessPct();
void setBrightnessPct(uint8_t pct);  // clamped; takes effect via power::applyBrightness()
void saveBrightness();               // persist the current value (on release, not while dragging)
uint8_t brightnessLevel();           // the same as a panel level, 0..255

// Speaker volume 0..100 % (0 = silent) and mute, for beeps and the Noise app.
// Both take effect via sound::applyVolume().
uint8_t volumePct();
void setVolumePct(uint8_t pct);  // clamped; unmutes
void saveVolume();               // persist volume and mute (on release, not while dragging)
bool muted();
void setMuted(bool on);  // saved immediately

bool hasWifi();  // credentials saved on the device
const char *wifiSsid();
const char *wifiPassword();
void setWifi(const char *ssid, const char *password);  // saved immediately

}  // namespace core::settings
