#pragma once
#include <stddef.h>
#include <stdint.h>

// Persistence (ARCHITECTURE §8.5):
// - RtcState lives in RTC memory: survives deep sleep, lost on power-off.
// - NVS blobs survive power-off; written only on user actions or before
//   sleep, never periodically (flash wear).
namespace core::store {

struct RtcState {
  uint32_t magic;
  uint8_t haStates[16];  // ha::LightState by list position (HA_MAX_LIGHTS), shown as "stale" after wake
};

void begin();
RtcState &rtc();
bool rtcValid();
void rtcCommit();  // mark the RTC state as valid

bool load(const char *key, void *data, size_t len);  // false if missing/size mismatch
bool save(const char *key, const void *data, size_t len);

}  // namespace core::store
