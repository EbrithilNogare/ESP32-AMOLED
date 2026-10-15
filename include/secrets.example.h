#pragma once

// ============================================================================
// Copy to secrets.h (gitignored) and fill in real values:
//   cp include/secrets.example.h include/secrets.h
// Without secrets.h the build falls back to this file, so CI stays green.
// ============================================================================

// Leave WIFI_SSID empty to run without Wi-Fi (the radio then never starts).
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
// More networks are added on the device (quick settings → Wi-Fi) and saved there.

// Optional static IP for WIFI_SSID. Leave empty for DHCP: the device remembers
// each network's lease and channel and reuses them to reconnect faster anyway.
#define WIFI_STATIC_IP ""
#define WIFI_GATEWAY ""
#define WIFI_SUBNET ""
#define WIFI_DNS ""

// Home Assistant: base URL and a long-lived access token
// (HA → Profile → Security → Long-lived access tokens). Use a dedicated HA user.
// Plain http on the LAN (no TLS client is built in). The host can be an IP
// address (most reliable), "homeassistant.local" (mDNS), or a bare name like
// "homeassistant", which is tried as "homeassistant.local" when the router's
// DNS doesn't know it. The lights themselves are loaded from Home Assistant.
#define HA_BASE_URL "http://192.168.1.10:8123"
#define HA_TOKEN ""

// Password for over-the-air updates (ArduinoOTA). Leave empty to disable OTA.
#define OTA_PASSWORD ""
