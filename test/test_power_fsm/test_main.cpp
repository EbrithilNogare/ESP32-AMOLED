#include <power_fsm.h>
#include <unity.h>

using namespace power;

// Same numbers as config.h / ARCHITECTURE §3.
static const Timings T = {10000, 15000, 20000, 60000, 300000};
static const Levels L = {255, 204, 64, 102, 38};

void setUp() {}
void tearDown() {}

static void test_battery_normal_timeline_boundaries() {
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(0, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(9999, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(10000, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(14999, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Black, stageFor(15000, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Black, stageFor(19999, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Sleep, stageFor(20000, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Sleep, stageFor(UINT32_MAX, b, n, T));
}

static void test_charger_normal_never_sleeps() {
  const Profile c = Profile::Charger;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(20000, c, n, T));  // battery sleep delay: irrelevant
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(59999, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(60000, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(299999, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Black, stageFor(300000, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Black, stageFor(UINT32_MAX, c, n, T));
}

static void test_keep_awake_always_active() {
  const uint32_t idles[] = {0, 10000, 20000, 300000, UINT32_MAX};
  for (uint32_t idle : idles) {
    TEST_ASSERT_EQUAL(Stage::Active, stageFor(idle, Profile::Battery, Policy::KeepAwake, T));
    TEST_ASSERT_EQUAL(Stage::Active, stageFor(idle, Profile::Charger, Policy::KeepAwake, T));
  }
}

static void test_low_power_display_minimal_never_sleep() {
  const Policy lp = Policy::LowPowerDisplay;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(9999, Profile::Battery, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(10000, Profile::Battery, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(20000, Profile::Battery, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(UINT32_MAX, Profile::Battery, lp, T));
}

static void test_policy_beats_profile_on_charger() {
  // A running pomodoro on the charger uses the *battery* dim delay, not 60 s.
  const Policy lp = Policy::LowPowerDisplay;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(9999, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(10000, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(300000, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextStage(9999, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(10000, Profile::Charger, lp, T));
  // ...but the charger's brightness cap still applies.
  const Levels bright = {255, 204, 64, 102, 250};
  TEST_ASSERT_EQUAL_UINT8(204, brightnessFor(Stage::Minimal, Profile::Charger, bright));
}

static void test_brightness_levels() {
  TEST_ASSERT_EQUAL_UINT8(255, brightnessFor(Stage::Active, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(204, brightnessFor(Stage::Active, Profile::Charger, L));
  TEST_ASSERT_EQUAL_UINT8(64, brightnessFor(Stage::Dimmed, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(102, brightnessFor(Stage::Dimmed, Profile::Charger, L));
  TEST_ASSERT_EQUAL_UINT8(0, brightnessFor(Stage::Black, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(0, brightnessFor(Stage::Black, Profile::Charger, L));
  TEST_ASSERT_EQUAL_UINT8(0, brightnessFor(Stage::Sleep, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(38, brightnessFor(Stage::Minimal, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(38, brightnessFor(Stage::Minimal, Profile::Charger, L));
}

static void test_brightness_capped_by_profile_max() {
  const Levels capped = {100, 50, 200, 200, 200};
  TEST_ASSERT_EQUAL_UINT8(100, brightnessFor(Stage::Dimmed, Profile::Battery, capped));
  TEST_ASSERT_EQUAL_UINT8(50, brightnessFor(Stage::Dimmed, Profile::Charger, capped));
  TEST_ASSERT_EQUAL_UINT8(100, brightnessFor(Stage::Minimal, Profile::Battery, capped));
  TEST_ASSERT_EQUAL_UINT8(50, brightnessFor(Stage::Minimal, Profile::Charger, capped));
}

static void test_ms_until_next_stage_battery() {
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL_UINT32(10000, msUntilNextStage(0, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextStage(9999, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(5000, msUntilNextStage(10000, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(5000, msUntilNextStage(15000, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextStage(19999, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(20000, b, n, T));
}

static void test_ms_until_next_stage_charger_and_keep_awake() {
  TEST_ASSERT_EQUAL_UINT32(60000, msUntilNextStage(0, Profile::Charger, Policy::Normal, T));
  TEST_ASSERT_EQUAL_UINT32(240000, msUntilNextStage(60000, Profile::Charger, Policy::Normal, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(300000, Profile::Charger, Policy::Normal, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(0, Profile::Battery, Policy::KeepAwake, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(0, Profile::Charger, Policy::KeepAwake, T));
}

static void test_ms_until_matches_stage_change() {
  // Walking by msUntilNextStage lands exactly on each stage change.
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  uint32_t idle = 0;
  Stage seen[4];
  int count = 0;
  seen[count++] = stageFor(idle, b, n, T);
  while (msUntilNextStage(idle, b, n, T) != UINT32_MAX && count < 4) {
    const uint32_t step = msUntilNextStage(idle, b, n, T);
    TEST_ASSERT_EQUAL(seen[count - 1], stageFor(idle + step - 1, b, n, T));
    idle += step;
    seen[count++] = stageFor(idle, b, n, T);
  }
  TEST_ASSERT_EQUAL(4, count);
  TEST_ASSERT_EQUAL(Stage::Dimmed, seen[1]);
  TEST_ASSERT_EQUAL(Stage::Black, seen[2]);
  TEST_ASSERT_EQUAL(Stage::Sleep, seen[3]);
}

static void test_zero_timings() {
  const Timings z = {0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL(Stage::Sleep, stageFor(0, Profile::Battery, Policy::Normal, z));
  TEST_ASSERT_EQUAL(Stage::Black, stageFor(0, Profile::Charger, Policy::Normal, z));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(0, Profile::Charger, Policy::LowPowerDisplay, z));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(0, Profile::Battery, Policy::Normal, z));
}

static void test_stage_names() {
  TEST_ASSERT_EQUAL_STRING("Active", stageName(Stage::Active));
  TEST_ASSERT_EQUAL_STRING("Dimmed", stageName(Stage::Dimmed));
  TEST_ASSERT_EQUAL_STRING("Black", stageName(Stage::Black));
  TEST_ASSERT_EQUAL_STRING("Minimal", stageName(Stage::Minimal));
  TEST_ASSERT_EQUAL_STRING("Sleep", stageName(Stage::Sleep));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_normal_timeline_boundaries);
  RUN_TEST(test_charger_normal_never_sleeps);
  RUN_TEST(test_keep_awake_always_active);
  RUN_TEST(test_low_power_display_minimal_never_sleep);
  RUN_TEST(test_policy_beats_profile_on_charger);
  RUN_TEST(test_brightness_levels);
  RUN_TEST(test_brightness_capped_by_profile_max);
  RUN_TEST(test_ms_until_next_stage_battery);
  RUN_TEST(test_ms_until_next_stage_charger_and_keep_awake);
  RUN_TEST(test_ms_until_matches_stage_change);
  RUN_TEST(test_zero_timings);
  RUN_TEST(test_stage_names);
  return UNITY_END();
}
