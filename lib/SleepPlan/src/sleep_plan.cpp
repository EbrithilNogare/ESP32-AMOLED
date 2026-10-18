#include "sleep_plan.h"

namespace sleepplan {

Need merge(const Need &a, const Need &b) {
  uint64_t wake = a.wakeAtMs;
  if (b.wakeAtMs && (!wake || b.wakeAtMs < wake)) wake = b.wakeAtMs;
  return {a.awake || b.awake, wake};
}

Plan decide(const Need &need, uint64_t nowMs, bool onCharger, Kind kind, const Config &c) {
  if (kind == Kind::PowerOff) return {Mode::Deep, 0};
  const uint64_t left = !need.wakeAtMs ? UINT64_MAX : need.wakeAtMs > nowMs ? need.wakeAtMs - nowMs : 0;
  const uint64_t soon = uint64_t(c.minDeepMs) + c.wakeLeadMs;
  if (need.awake || onCharger || left < soon) return {Mode::Doze, left < c.dozePollMs ? left : c.dozePollMs};
  return {Mode::Deep, left == UINT64_MAX ? 0 : left - c.wakeLeadMs};
}

}  // namespace sleepplan
