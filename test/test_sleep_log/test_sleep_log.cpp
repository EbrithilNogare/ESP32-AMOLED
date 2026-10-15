#include <sleep_log.h>
#include <unity.h>

using namespace sleeplog;

void setUp() {}
void tearDown() {}

static constexpr int64_t kT0 = 1790000000;  // some date in 2026
static constexpr int64_t kHour = 3600, kDay = 86400;

static void test_a_sleep_on_battery_is_counted() {
  Log log;
  enter(log, kT0, 80, 3950, false);
  TEST_ASSERT_TRUE(wake(log, kT0 + 10 * kHour, 79, 3940, false));
  TEST_ASSERT_EQUAL_UINT32(10 * kHour, log.last.seconds);
  TEST_ASSERT_EQUAL_INT8(80, log.last.fromPct);
  TEST_ASSERT_EQUAL_INT8(79, log.last.toPct);
  TEST_ASSERT_EQUAL_UINT16(3940, log.last.toMv);
  TEST_ASSERT_EQUAL_INT16(1, log.totalLost);
  TEST_ASSERT_EQUAL_INT8(-1, log.enterPct);  // a second wake without a sleep counts nothing
  TEST_ASSERT_FALSE(wake(log, kT0 + 11 * kHour, 79, 3940, false));
}

static void test_sleeps_add_up_and_give_days_per_charge() {
  Log log;
  // Three nights, 3 % lost asleep in 30 h; the awake drain between doesn't count.
  enter(log, kT0, 90, 0, false);
  wake(log, kT0 + 10 * kHour, 89, 0, false);
  enter(log, kT0 + kDay, 85, 0, false);
  wake(log, kT0 + kDay + 10 * kHour, 84, 0, false);
  enter(log, kT0 + 2 * kDay, 80, 0, false);
  wake(log, kT0 + 2 * kDay + 10 * kHour, 79, 0, false);
  TEST_ASSERT_EQUAL_UINT32(30 * kHour, log.totalS);
  const Estimate e = daysPerCharge(log);
  TEST_ASSERT_FALSE(e.atLeast);
  TEST_ASSERT_EQUAL_UINT32(41, e.days);  // 1 % per 10 h = 1000 h ≈ 41.7 days
}

static void test_too_little_data_gives_no_estimate() {
  Log log;
  enter(log, kT0, 80, 0, false);
  wake(log, kT0 + 8 * kHour, 79, 0, false);
  TEST_ASSERT_EQUAL_UINT32(0, daysPerCharge(log).days);
}

static void test_no_loss_over_days_is_a_lower_bound() {
  Log log;
  enter(log, kT0, 80, 0, false);
  wake(log, kT0 + 3 * kDay, 80, 0, false);
  const Estimate e = daysPerCharge(log);
  TEST_ASSERT_TRUE(e.atLeast);
  TEST_ASSERT_EQUAL_UINT32(300, e.days);  // under 1 % in 3 days
}

static void test_charging_restarts_the_totals() {
  Log log;
  enter(log, kT0, 80, 0, false);
  wake(log, kT0 + kDay, 77, 0, false);
  TEST_ASSERT_EQUAL_INT16(3, log.totalLost);

  enter(log, kT0 + 2 * kDay, 70, 0, true);  // on the charger: not measured
  TEST_ASSERT_EQUAL_INT16(0, log.totalLost);
  TEST_ASSERT_FALSE(wake(log, kT0 + 3 * kDay, 100, 0, false));

  enter(log, kT0, 50, 0, false);  // plugged in while asleep
  TEST_ASSERT_FALSE(wake(log, kT0 + kDay, 50, 0, true));
  TEST_ASSERT_EQUAL_UINT32(0, log.totalS);

  enter(log, kT0, 50, 0, false);  // charged while asleep and unplugged again
  wake(log, kT0 + kDay, 60, 0, false);
  TEST_ASSERT_EQUAL_UINT32(0, log.totalS);
}

static void test_settling_one_percent_up_counts_as_no_loss() {
  Log log;
  enter(log, kT0, 50, 0, false);
  TEST_ASSERT_TRUE(wake(log, kT0 + kDay, 51, 0, false));
  TEST_ASSERT_EQUAL_INT16(0, log.totalLost);
  TEST_ASSERT_EQUAL_UINT32(kDay, log.totalS);
}

static void test_a_clock_jump_is_ignored() {
  Log log;
  enter(log, kT0, 50, 0, false);
  TEST_ASSERT_FALSE(wake(log, kT0 - 10, 49, 0, false));  // went backwards
  enter(log, 0, 50, 0, false);                            // 1970 before SNTP, then synced
  TEST_ASSERT_FALSE(wake(log, kT0, 49, 0, false));
}

static void test_no_gauge_is_not_measured() {
  Log log;
  enter(log, kT0, -1, 0, false);
  TEST_ASSERT_FALSE(wake(log, kT0 + kDay, 50, 0, false));
  enter(log, kT0, 50, 0, false);
  TEST_ASSERT_FALSE(wake(log, kT0 + kDay, -1, 0, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_sleep_on_battery_is_counted);
  RUN_TEST(test_sleeps_add_up_and_give_days_per_charge);
  RUN_TEST(test_too_little_data_gives_no_estimate);
  RUN_TEST(test_no_loss_over_days_is_a_lower_bound);
  RUN_TEST(test_charging_restarts_the_totals);
  RUN_TEST(test_settling_one_percent_up_counts_as_no_loss);
  RUN_TEST(test_a_clock_jump_is_ignored);
  RUN_TEST(test_no_gauge_is_not_measured);
  return UNITY_END();
}
