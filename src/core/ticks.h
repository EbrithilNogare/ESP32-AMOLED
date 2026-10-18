#pragma once
#include <stdint.h>

// millis() wraps after 49.7 days, which a board on the charger reaches.
// Compare deadlines by signed difference, never with `now >= deadline`.
namespace core {

inline bool reached(uint32_t nowMs, uint32_t deadlineMs) { return int32_t(nowMs - deadlineMs) >= 0; }

// A 64-bit millisecond clock from 32-bit samples, for logic that stores
// absolute timestamps (lib/PomodoroCore). It has to see a sample at least
// every 24 days; the main loop and the light-sleep wakes see many more.
// A sample slightly older than the newest one (taken earlier in the same
// loop pass) maps to the right epoch too.
inline uint64_t extend(uint32_t ms) {
  static uint64_t epoch = 0;  // high bits of the newest sample
  static uint32_t newest = 0;
  static bool started = false;
  if (!started || int32_t(ms - newest) > 0) {
    started = true;
    if (ms < newest) epoch += 1ULL << 32;
    newest = ms;
    return epoch | ms;
  }
  return (ms <= newest || epoch == 0 ? epoch : epoch - (1ULL << 32)) | ms;
}

// extend(), carried on through deep sleep: after a wake it continues from
// where it was plus the time asleep (core::clock). Anything that has to
// outlive a sleep keeps its timestamps on this clock (TimerCore,
// PomodoroCore), so a running countdown can be parked in RTC memory as is.
uint64_t mono(uint32_t ms);

}  // namespace core
