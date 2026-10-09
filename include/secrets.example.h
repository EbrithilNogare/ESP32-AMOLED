#pragma once

// ============================================================================
// Copy to secrets.h (gitignored) and fill in real values:
//   cp include/secrets.example.h include/secrets.h
// Without secrets.h the build falls back to this file, so CI stays green.
// ============================================================================

#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASSWORD   "your-wifi-password"

// Password for over-the-air updates (ArduinoOTA). Leave empty to disable.
#define OTA_PASSWORD    ""
