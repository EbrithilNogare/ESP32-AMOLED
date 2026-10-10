#include "power_fsm.h"

namespace power {

namespace {

// One step of a timeline: the stage entered once idle >= atMs.
struct Step {
  uint32_t atMs;
  Stage stage;
};

// The timeline for a profile/policy pair, in order. Returns the step count.
// KeepAwake has no steps (Active forever).
uint8_t timelineRaw(Profile profile, Policy policy, const Timings &t, Step out[3]) {
  switch (policy) {
    case Policy::KeepAwake: return 0;
    case Policy::LowPowerDisplay:
      // Battery dim delay in both profiles: policies beat profiles (§3.1).
      out[0] = {t.batDimMs, Stage::Minimal};
      return 1;
    case Policy::Normal: break;
  }
  if (profile == Profile::Charger) {
    out[0] = {t.chgDimMs, Stage::Dimmed};
    out[1] = {t.chgBlackMs, Stage::Black};
    return 2;
  }
  out[0] = {t.batDimMs, Stage::Dimmed};
  out[1] = {t.batBlackMs, Stage::Black};
  out[2] = {t.batSleepMs, Stage::Sleep};
  return 3;
}

// Thresholds are clamped to be non-decreasing, so a deeper stage never comes
// before a shallower one (e.g. black < dim → both at the dim threshold).
uint8_t timeline(Profile profile, Policy policy, const Timings &t, Step out[3]) {
  const uint8_t n = timelineRaw(profile, policy, t, out);
  for (uint8_t i = 1; i < n; i++)
    if (out[i].atMs < out[i - 1].atMs) out[i].atMs = out[i - 1].atMs;
  return n;
}

uint8_t minU8(uint8_t a, uint8_t b) { return a < b ? a : b; }

}  // namespace

Stage stageFor(uint32_t idleMs, Profile profile, Policy policy, const Timings &t) {
  Step steps[3];
  const uint8_t n = timeline(profile, policy, t, steps);
  // Steps run shallow to deep, so the deepest reached stage wins.
  Stage stage = Stage::Active;
  for (uint8_t i = 0; i < n; i++) {
    if (idleMs >= steps[i].atMs) stage = steps[i].stage;
  }
  return stage;
}

uint8_t brightnessFor(Stage stage, Profile profile, const Levels &l) {
  const bool chg = profile == Profile::Charger;
  const uint8_t max = chg ? l.maxCharger : l.maxBattery;
  switch (stage) {
    case Stage::Active: return max;
    case Stage::Dimmed: return minU8(chg ? l.dimCharger : l.dimBattery, max);
    case Stage::Minimal: return minU8(l.minimal, max);
    case Stage::Black:
    case Stage::Sleep: break;
  }
  return 0;
}

uint32_t msUntilNextStage(uint32_t idleMs, Profile profile, Policy policy, const Timings &t) {
  Step steps[3];
  const uint8_t n = timeline(profile, policy, t, steps);
  uint32_t best = UINT32_MAX;
  for (uint8_t i = 0; i < n; i++) {
    if (steps[i].atMs > idleMs && steps[i].atMs - idleMs < best) best = steps[i].atMs - idleMs;
  }
  return best;
}

const char *stageName(Stage stage) {
  switch (stage) {
    case Stage::Active: return "Active";
    case Stage::Dimmed: return "Dimmed";
    case Stage::Black: return "Black";
    case Stage::Minimal: return "Minimal";
    case Stage::Sleep: return "Sleep";
  }
  return "?";
}

}  // namespace power
