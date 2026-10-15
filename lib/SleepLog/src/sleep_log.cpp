#include "sleep_log.h"

namespace sleeplog {

static constexpr int64_t kMaxSleepS = 366LL * 86400;  // longer = a clock that jumped
static constexpr uint32_t kDayS = 86400;
static constexpr int kMinLost = 2;  // the gauge reads whole percent: 1 % alone is mostly rounding

static void restart(Log &log) {
  log.totalS = 0;
  log.totalLost = 0;
}

void enter(Log &log, int64_t now, int pct, uint16_t mv, bool charger) {
  if (charger) restart(log);
  log.enterTime = now;
  log.enterPct = charger || pct < 0 || pct > 100 ? int8_t(-1) : int8_t(pct);
  log.enterMv = mv;
}

bool wake(Log &log, int64_t now, int pct, uint16_t mv, bool charger) {
  const int from = log.enterPct;
  log.enterPct = -1;
  if (from < 0 || pct < 0 || pct > 100) return false;
  const int64_t s = now - log.enterTime;
  // Charged while asleep (a level 1 % up is the gauge settling after the load
  // went away, not a charge), or the clock was reset: nothing to compare.
  if (charger || pct > from + 1 || s <= 0 || s > kMaxSleepS) {
    restart(log);
    return false;
  }
  log.last = {uint32_t(s), int8_t(from), int8_t(pct), log.enterMv, mv};
  log.totalS += uint32_t(s);
  log.totalLost += int16_t(from - pct);
  if (log.totalLost < 0) log.totalLost = 0;
  return true;
}

Estimate daysPerCharge(const Log &log) {
  if (log.totalLost >= kMinLost) return {uint32_t(uint64_t(log.totalS) * 100 / (uint64_t(log.totalLost) * kDayS)), false};
  if (log.totalS < kDayS) return {0, false};
  // Under 2 % in this long: the drain is at most (lost + 1) % over totalS.
  return {uint32_t(uint64_t(log.totalS) * 100 / (uint64_t(log.totalLost + 1) * kDayS)), true};
}

}  // namespace sleeplog
