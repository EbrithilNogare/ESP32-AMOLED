#include <power_fsm.h>
#include <unity.h>

using namespace power;

// Same numbers as config.h / ARCHITECTURE §3.
static const Timings T = {30000, 40000, 120000, 600000};
static const Levels L = {255, 204, 64, 102, 38};

void setUp() {}
void tearDown() {}

static void test_battery_normal_timeline_boundaries() {
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(0, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(29999, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(30000, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(39999, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(40000, b, n, T));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(UINT32_MAX, b, n, T));
}

static void test_charger_normal_timeline() {
  const Profile c = Profile::Charger;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(40000, c, n, T));  // battery delays: irrelevant
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(119999, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(120000, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Dimmed, stageFor(599999, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(600000, c, n, T));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(UINT32_MAX, c, n, T));
}

static void test_keep_awake_always_active() {
  const uint32_t idles[] = {0, 30000, 40000, 600000, UINT32_MAX};
  for (uint32_t idle : idles) {
    TEST_ASSERT_EQUAL(Stage::Active, stageFor(idle, Profile::Battery, Policy::KeepAwake, T));
    TEST_ASSERT_EQUAL(Stage::Active, stageFor(idle, Profile::Charger, Policy::KeepAwake, T));
  }
}

static void test_low_power_display_stays_minimal() {
  const Policy lp = Policy::LowPowerDisplay;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(29999, Profile::Battery, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(30000, Profile::Battery, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(40000, Profile::Battery, lp, T));  // never Off by itself
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(UINT32_MAX, Profile::Battery, lp, T));
}

static void test_policy_beats_profile_on_charger() {
  // A running pomodoro on the charger uses the *battery* dim delay, not 2 min.
  const Policy lp = Policy::LowPowerDisplay;
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(29999, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(30000, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(600000, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextStage(29999, Profile::Charger, lp, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(30000, Profile::Charger, lp, T));
  // ...but the charger's brightness cap still applies.
  const Levels bright = {255, 204, 64, 102, 250};
  TEST_ASSERT_EQUAL_UINT8(204, brightnessFor(Stage::Minimal, Profile::Charger, bright));
}

static void test_brightness_levels() {
  TEST_ASSERT_EQUAL_UINT8(255, brightnessFor(Stage::Active, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(204, brightnessFor(Stage::Active, Profile::Charger, L));
  TEST_ASSERT_EQUAL_UINT8(64, brightnessFor(Stage::Dimmed, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(102, brightnessFor(Stage::Dimmed, Profile::Charger, L));
  TEST_ASSERT_EQUAL_UINT8(0, brightnessFor(Stage::Off, Profile::Battery, L));
  TEST_ASSERT_EQUAL_UINT8(0, brightnessFor(Stage::Off, Profile::Charger, L));
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

static void test_ms_until_next_stage() {
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  TEST_ASSERT_EQUAL_UINT32(30000, msUntilNextStage(0, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextStage(29999, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(10000, msUntilNextStage(30000, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(40000, b, n, T));
  TEST_ASSERT_EQUAL_UINT32(120000, msUntilNextStage(0, Profile::Charger, n, T));
  TEST_ASSERT_EQUAL_UINT32(480000, msUntilNextStage(120000, Profile::Charger, n, T));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(0, b, Policy::KeepAwake, T));
}

static void test_ms_until_matches_stage_change() {
  // Walking by msUntilNextStage lands exactly on each stage change.
  const Profile b = Profile::Battery;
  const Policy n = Policy::Normal;
  uint32_t idle = 0;
  Stage seen[3];
  int count = 0;
  seen[count++] = stageFor(idle, b, n, T);
  while (msUntilNextStage(idle, b, n, T) != UINT32_MAX && count < 3) {
    const uint32_t step = msUntilNextStage(idle, b, n, T);
    TEST_ASSERT_EQUAL(seen[count - 1], stageFor(idle + step - 1, b, n, T));
    idle += step;
    seen[count++] = stageFor(idle, b, n, T);
  }
  TEST_ASSERT_EQUAL(3, count);
  TEST_ASSERT_EQUAL(Stage::Dimmed, seen[1]);
  TEST_ASSERT_EQUAL(Stage::Off, seen[2]);
}

static void test_zero_timings() {
  const Timings z = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(0, Profile::Battery, Policy::Normal, z));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(0, Profile::Charger, Policy::Normal, z));
  TEST_ASSERT_EQUAL(Stage::Minimal, stageFor(0, Profile::Charger, Policy::LowPowerDisplay, z));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(0, Profile::Battery, Policy::Normal, z));
}

static void test_out_of_order_thresholds_are_clamped() {
  // Off below dim: off waits for the dim threshold, nothing before it.
  const Timings o = {20000, 10000, 5000, 1000};
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(10000, Profile::Battery, Policy::Normal, o));
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(19999, Profile::Battery, Policy::Normal, o));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(20000, Profile::Battery, Policy::Normal, o));
  TEST_ASSERT_EQUAL_UINT32(20000, msUntilNextStage(0, Profile::Battery, Policy::Normal, o));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, msUntilNextStage(20000, Profile::Battery, Policy::Normal, o));
  TEST_ASSERT_EQUAL(Stage::Active, stageFor(1000, Profile::Charger, Policy::Normal, o));
  TEST_ASSERT_EQUAL(Stage::Off, stageFor(5000, Profile::Charger, Policy::Normal, o));
}

static void test_stage_names() {
  TEST_ASSERT_EQUAL_STRING("Active", stageName(Stage::Active));
  TEST_ASSERT_EQUAL_STRING("Dimmed", stageName(Stage::Dimmed));
  TEST_ASSERT_EQUAL_STRING("Minimal", stageName(Stage::Minimal));
  TEST_ASSERT_EQUAL_STRING("Off", stageName(Stage::Off));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_normal_timeline_boundaries);
  RUN_TEST(test_charger_normal_timeline);
  RUN_TEST(test_keep_awake_always_active);
  RUN_TEST(test_low_power_display_stays_minimal);
  RUN_TEST(test_policy_beats_profile_on_charger);
  RUN_TEST(test_brightness_levels);
  RUN_TEST(test_brightness_capped_by_profile_max);
  RUN_TEST(test_ms_until_next_stage);
  RUN_TEST(test_ms_until_matches_stage_change);
  RUN_TEST(test_zero_timings);
  RUN_TEST(test_out_of_order_thresholds_are_clamped);
  RUN_TEST(test_stage_names);
  return UNITY_END();
}
