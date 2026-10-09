#pragma once

// ============================================================================
// Copy to secrets.h (gitignored) and fill in real values:
//   cp include/secrets.example.h include/secrets.h
// Without secrets.h the build falls back to this file, so CI stays green.
// ============================================================================

#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASSWORD   "your-wifi-password"

// Optional static IP: speeds up reconnecting after deep sleep. Leave empty for DHCP.
#define WIFI_STATIC_IP  ""
#define WIFI_GATEWAY    ""
#define WIFI_SUBNET     ""
#define WIFI_DNS        ""

// Home Assistant: base URL and a long-lived access token
// (HA → Profile → Security → Long-lived access tokens). Use a dedicated HA user.
// Plain http on the LAN (no TLS client is built in); an IP address avoids
// relying on mDNS for "homeassistant.local".
#define HA_BASE_URL     "http://192.168.1.10:8123"
#define HA_TOKEN        ""

// Password for over-the-air updates (ArduinoOTA). Leave empty to disable OTA.
#define OTA_PASSWORD    ""
