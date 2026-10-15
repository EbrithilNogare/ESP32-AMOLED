#pragma once
#include <stddef.h>
#include <stdint.h>

// User settings changed on the device (quick settings, Wi-Fi screen), kept in
// NVS so they survive reboots, deep sleep and firmware updates (USB or OTA
// rewrite only the app partition). Only "erase flash" clears them.
// Saved Wi-Fi networks come before the ones in secrets.h (core::net).
namespace core::settings {

constexpr size_t kSsidMax = 32;      // 802.11 limit
constexpr size_t kPasswordMax = 63;  // WPA2 passphrase limit
constexpr int kWifiSaved = 5;        // networks remembered on the device

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

// Wi-Fi networks saved on the device, most recently joined first.
int wifiCount();
const char *wifiSsid(int i);
const char *wifiPassword(int i);
// Saved immediately, at the front; past kWifiSaved the oldest one drops off.
void saveWifi(const char *ssid, const char *password);

}  // namespace core::settings
