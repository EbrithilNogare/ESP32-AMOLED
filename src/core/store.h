#pragma once
#include <sleep_log.h>
#include <stddef.h>
#include <stdint.h>

// Persistence (ARCHITECTURE §8.5):
// - RtcState lives in RTC memory: survives deep sleep (and so a power-off by
//   the button, which is a deep sleep too), lost on a reset or a flat battery.
// - NVS blobs survive everything but "erase flash"; written only on user
//   actions or before sleep, never periodically (flash wear).
namespace core::store {

constexpr size_t kParkKeys = 12;     // apps that can park state in RTC memory
constexpr size_t kParkBytes = 1536;  // their data, all together
constexpr size_t kKeyMax = 11;

struct RtcState {
  uint32_t magic = 0;
  uint8_t haStates[16];  // ha::LightState by list position (HA_MAX_LIGHTS), shown as "stale" after wake
  sleeplog::Log sleep;   // battery drain while asleep (core::power)
  // Resuming after a deep sleep (core::power, apps::manager).
  uint64_t monoAtSleep;       // core::mono() going to sleep: it carries on from here
  int64_t wallAtSleepMs;      // the wall clock then, to add the time asleep
  char lastApp[kKeyMax + 1];  // the app on screen, reopened by a button wake ("" = menu)
  uint8_t sleepKind;          // sleepplan::Kind of the last sleep
  // App state parked for the sleep (park() / unpark()).
  struct Slot {
    char key[kKeyMax + 1];
    uint16_t offset, len;
  } slots[kParkKeys];
  uint8_t parked[kParkBytes];
  uint16_t parkedUsed;
};

void begin();
RtcState &rtc();
bool rtcValid();
void rtcCommit();  // mark the RTC state as valid

bool load(const char *key, void *data, size_t len);  // false if missing/size mismatch
bool save(const char *key, const void *data, size_t len);

// RTC memory, for state that only has to outlive a deep sleep (a running
// countdown, the last roll): no flash wear, gone after a reset. Same
// contract as load()/save(); a key keeps the size it was first parked with.
bool park(const char *key, const void *data, size_t len);
bool unpark(const char *key, void *data, size_t len);

}  // namespace core::store
