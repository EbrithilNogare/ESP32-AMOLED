// Deep-sleep drain (docs/ARCHITECTURE.md §8.6): the battery level and the
// wall clock are noted when the device goes to sleep and again when it
// wakes, so the device can tell how long a charge lasts asleep without a
// current meter. Only sleeps on battery count; a charge starts over.
#pragma once

#include <stdint.h>

namespace sleeplog {

struct Sleep {
  uint32_t seconds = 0;
  int8_t fromPct = -1, toPct = -1;  // -1 = nothing recorded
  uint16_t fromMv = 0, toMv = 0;
};

// Lives in RTC memory, so it survives deep sleep (not a power-off).
struct Log {
  int64_t enterTime = 0;  // the sleep in progress, unix seconds
  int8_t enterPct = -1;   // -1 = not on battery (or no gauge): don't count it
  uint16_t enterMv = 0;
  Sleep last;              // the latest counted sleep
  uint32_t totalS = 0;     // all counted sleeps since the last charge
  int16_t totalLost = 0;   // percent lost during them
};

// Going to sleep. On the charger nothing is measured and the totals restart.
void enter(Log &log, int64_t now, int pct, uint16_t mv, bool charger);

// Woke up; true if the sleep was counted. A higher level than before (it
// charged meanwhile) or a clock that went backwards restarts the totals.
bool wake(Log &log, int64_t now, int pct, uint16_t mv, bool charger);

struct Estimate {
  uint32_t days;  // 0 = not enough data yet (under 2 % lost and under a day asleep)
  bool atLeast;   // under 2 % lost: `days` is a lower bound
};
// How many days a full charge lasts asleep at the measured rate.
Estimate daysPerCharge(const Log &log);

}  // namespace sleeplog
