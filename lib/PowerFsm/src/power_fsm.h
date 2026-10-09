// Idle timeline: which power stage the device is in, and how bright the
// panel should be, given idle time, power profile and the app's policy.
// Pure functions, no state: the PowerManager owns the idle clock and the
// fades; this only decides (docs/ARCHITECTURE.md §3, §8).
#pragma once

#include <stdint.h>

namespace power {

enum class Profile : uint8_t { Battery, Charger };
enum class Policy : uint8_t { Normal, KeepAwake, LowPowerDisplay };
enum class Stage : uint8_t { Active, Dimmed, Black, Minimal, Sleep };

// Idle thresholds in ms (time since the last touch/button).
struct Timings {
  uint32_t batDimMs, batBlackMs, batSleepMs, chgDimMs, chgBlackMs;
};

// Brightness levels, 0..255 (panel command 0x51).
struct Levels {
  uint8_t maxBattery, maxCharger, dimBattery, dimCharger, minimal;
};

// Stage reached after idleMs. A threshold counts as reached at exactly its ms.
// Policies beat profiles: KeepAwake and LowPowerDisplay ignore the profile.
Stage stageFor(uint32_t idleMs, Profile profile, Policy policy, const Timings &t);

// Target brightness for a stage. The profile's max caps every level.
uint8_t brightnessFor(Stage stage, Profile profile, const Levels &l);

// ms until the next stage threshold, or UINT32_MAX if the current stage is
// final. Lets the caller sleep/wait instead of polling.
uint32_t msUntilNextStage(uint32_t idleMs, Profile profile, Policy policy, const Timings &t);

const char *stageName(Stage stage);

}  // namespace power
