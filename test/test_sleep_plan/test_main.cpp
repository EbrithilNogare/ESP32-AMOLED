#include <sleep_plan.h>
#include <unity.h>

using namespace sleepplan;

// Same numbers as config.h (SLEEP_*).
static const Config C = {5000, 600, 5000};
static constexpr uint64_t T0 = 1000000;  // "now" on the monotonic clock

void setUp() {}
void tearDown() {}

static void test_merge_takes_awake_and_earliest_wake() {
  const Need none{false, 0};
  const Need later{false, T0 + 90000}, sooner{false, T0 + 30000}, sound{true, 0};
  Need m = merge(none, none);
  TEST_ASSERT_FALSE(m.awake);
  TEST_ASSERT_EQUAL_UINT64(0, m.wakeAtMs);
  m = merge(later, sooner);
  TEST_ASSERT_EQUAL_UINT64(T0 + 30000, m.wakeAtMs);
  m = merge(sooner, later);
  TEST_ASSERT_EQUAL_UINT64(T0 + 30000, m.wakeAtMs);
  m = merge(none, later);  // 0 means "no deadline", not "the earliest"
  TEST_ASSERT_EQUAL_UINT64(T0 + 90000, m.wakeAtMs);
  m = merge(later, sound);
  TEST_ASSERT_TRUE(m.awake);
  TEST_ASSERT_EQUAL_UINT64(T0 + 90000, m.wakeAtMs);
}

static void test_nothing_needed_deep_sleeps_until_the_button() {
  const Plan p = decide({false, 0}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Deep, p.mode);
  TEST_ASSERT_EQUAL_UINT64(0, p.ms);
}

static void test_deadline_deep_sleeps_with_a_timer_a_little_early() {
  const Plan p = decide({false, T0 + 25 * 60000}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Deep, p.mode);
  TEST_ASSERT_EQUAL_UINT64(25 * 60000 - 600, p.ms);
}

static void test_close_deadline_dozes_instead() {
  // 5.6 s is the boundary: minDeepMs + wakeLeadMs.
  Plan p = decide({false, T0 + 5599}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
  TEST_ASSERT_EQUAL_UINT64(5000, p.ms);  // a light sleep never exceeds the poll period
  p = decide({false, T0 + 5600}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Deep, p.mode);
  TEST_ASSERT_EQUAL_UINT64(5000, p.ms);
  p = decide({false, T0 + 1200}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
  TEST_ASSERT_EQUAL_UINT64(1200, p.ms);  // light sleep exactly until it's due
}

static void test_overdue_deadline_dozes_with_no_sleep() {
  const Plan p = decide({false, T0 - 50}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
  TEST_ASSERT_EQUAL_UINT64(0, p.ms);
}

static void test_sound_keeps_it_dozing() {
  Plan p = decide({true, 0}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
  TEST_ASSERT_EQUAL_UINT64(5000, p.ms);
  p = decide({true, T0 + 3600000}, T0, false, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
}

static void test_charger_dozes() {
  Plan p = decide({false, 0}, T0, true, Kind::Sleep, C);
  TEST_ASSERT_EQUAL(Mode::Doze, p.mode);
  TEST_ASSERT_EQUAL_UINT64(5000, p.ms);
  p = decide({false, T0 + 2000}, T0, true, Kind::Sleep, C);
  TEST_ASSERT_EQUAL_UINT64(2000, p.ms);
}

static void test_power_off_ignores_everything() {
  const Need busy{true, T0 + 1000};
  Plan p = decide(busy, T0, true, Kind::PowerOff, C);
  TEST_ASSERT_EQUAL(Mode::Deep, p.mode);
  TEST_ASSERT_EQUAL_UINT64(0, p.ms);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_merge_takes_awake_and_earliest_wake);
  RUN_TEST(test_nothing_needed_deep_sleeps_until_the_button);
  RUN_TEST(test_deadline_deep_sleeps_with_a_timer_a_little_early);
  RUN_TEST(test_close_deadline_dozes_instead);
  RUN_TEST(test_overdue_deadline_dozes_with_no_sleep);
  RUN_TEST(test_sound_keeps_it_dozing);
  RUN_TEST(test_charger_dozes);
  RUN_TEST(test_power_off_ignores_everything);
  return UNITY_END();
}
