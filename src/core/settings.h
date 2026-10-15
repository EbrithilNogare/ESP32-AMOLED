#pragma once
#include <stddef.h>
#include <stdint.h>
#include <touch_cal.h>

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

// A Wi-Fi network saved on the device, with what core::net learned the last
// time it connected, so the next connect can skip the scan and DHCP.
struct WifiNet {
  char ssid[kSsidMax + 1];
  char password[kPasswordMax + 1];
  uint8_t channel;   // 0: unknown
  uint8_t bssid[6];  // the access point it was on
  uint32_t ip, gateway, mask, dns;  // the DHCP lease (lwIP byte order); ip 0: none
  uint32_t leasedAt;                // unix time of that lease; 0: unknown
};

// Saved networks, most recently joined first.
int wifiCount();
const WifiNet &wifi(int i);
// Saved immediately, at the front, replacing a network with the same name;
// past kWifiSaved the oldest one drops off.
void saveWifi(const WifiNet &net);

// Touch calibration (System → Touch); identity until calibrated.
const touchcal::Cal &touchCal();
void saveTouchCal(const touchcal::Cal &c);  // saved immediately

}  // namespace core::settings
