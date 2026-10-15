#include <timer_core.h>
#include <unity.h>

using namespace timer;

void setUp() {}
void tearDown() {}

static const uint64_t T0 = 0x1'0000'0000ULL - 1000;  // across the 32-bit wrap

static void test_stopwatch_runs_pauses_resumes() {
  Stopwatch s;
  TEST_ASSERT_TRUE(s.zero());
  s.toggle(T0);
  TEST_ASSERT_TRUE(s.running());
  TEST_ASSERT_EQUAL_UINT64(1500, s.elapsedMs(T0 + 1500));
  s.toggle(T0 + 2000);  // pause at 2 s
  TEST_ASSERT_FALSE(s.running());
  TEST_ASSERT_EQUAL_UINT64(2000, s.elapsedMs(T0 + 60000));  // frozen while paused
  s.toggle(T0 + 60000);
  TEST_ASSERT_EQUAL_UINT64(2500, s.elapsedMs(T0 + 60500));
  s.reset();
  TEST_ASSERT_TRUE(s.zero());
  TEST_ASSERT_EQUAL_UINT64(0, s.elapsedMs(T0 + 70000));
}

static void test_countdown_set_is_clamped() {
  Countdown c;
  c.set(100 * 60);
  TEST_ASSERT_EQUAL_UINT32(kMaxSeconds, c.seconds());
  c.set(0);
  TEST_ASSERT_FALSE(c.start(T0));
  TEST_ASSERT_EQUAL(State::Idle, c.state());
}

static void test_countdown_steps_wrap_without_carry() {
  Countdown c;
  c.set(0);
  c.stepSeconds(-1);
  TEST_ASSERT_EQUAL_UINT32(59, c.seconds());  // 0:59, minutes untouched
  c.stepSeconds(1);
  TEST_ASSERT_EQUAL_UINT32(0, c.seconds());
  c.stepMinutes(-1);
  TEST_ASSERT_EQUAL_UINT32(99 * 60, c.seconds());
  c.stepSeconds(-1);
  TEST_ASSERT_EQUAL_UINT32(kMaxSeconds, c.seconds());
  c.stepMinutes(1);
  TEST_ASSERT_EQUAL_UINT32(59, c.seconds());
  c.stepMinutes(250);                                   // big drags wrap too
  TEST_ASSERT_EQUAL_UINT32(50 * 60 + 59, c.seconds());  // (0 + 250) % 100
}

static void test_countdown_runs_to_done_once() {
  Countdown c;
  c.set(90);
  TEST_ASSERT_EQUAL_UINT64(90000, c.remainingMs(T0));
  TEST_ASSERT_TRUE(c.start(T0));
  TEST_ASSERT_EQUAL_UINT64(60000, c.remainingMs(T0 + 30000));
  TEST_ASSERT_FALSE(c.update(T0 + 89999));
  TEST_ASSERT_EQUAL_UINT64(1, c.remainingMs(T0 + 89999));
  TEST_ASSERT_TRUE(c.update(T0 + 90000));
  TEST_ASSERT_FALSE(c.update(T0 + 90001));  // reported once
  TEST_ASSERT_EQUAL(State::Done, c.state());
  TEST_ASSERT_EQUAL_UINT64(0, c.remainingMs(T0 + 95000));
  TEST_ASSERT_EQUAL_UINT64(5000, c.overMs(T0 + 95000));
  c.toggle(T0 + 96000);  // dismiss
  TEST_ASSERT_EQUAL(State::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT64(90000, c.remainingMs(T0 + 96000));  // ready to go again
}

static void test_countdown_late_update_still_fires() {
  Countdown c;
  c.set(10);
  c.start(T0);
  TEST_ASSERT_TRUE(c.update(T0 + 60000));  // e.g. after a long light sleep
  TEST_ASSERT_EQUAL_UINT64(50000, c.overMs(T0 + 60000));
}

static void test_countdown_pause_freezes() {
  Countdown c;
  c.set(60);
  c.toggle(T0);  // start
  c.toggle(T0 + 20000);
  TEST_ASSERT_EQUAL(State::Paused, c.state());
  TEST_ASSERT_EQUAL_UINT64(40000, c.remainingMs(T0 + 500000));
  TEST_ASSERT_FALSE(c.update(T0 + 500000));
  c.toggle(T0 + 500000);
  TEST_ASSERT_TRUE(c.running());
  TEST_ASSERT_EQUAL_UINT64(10000, c.remainingMs(T0 + 530000));
  TEST_ASSERT_TRUE(c.update(T0 + 540000));
}

static void test_countdown_reset_and_set_stop_a_run() {
  Countdown c;
  c.set(60);
  c.start(T0);
  c.reset();
  TEST_ASSERT_EQUAL(State::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT32(60, c.seconds());
  c.start(T0);
  c.stepSeconds(1);
  TEST_ASSERT_EQUAL(State::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT32(61, c.seconds());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stopwatch_runs_pauses_resumes);
  RUN_TEST(test_countdown_set_is_clamped);
  RUN_TEST(test_countdown_steps_wrap_without_carry);
  RUN_TEST(test_countdown_runs_to_done_once);
  RUN_TEST(test_countdown_late_update_still_fires);
  RUN_TEST(test_countdown_pause_freezes);
  RUN_TEST(test_countdown_reset_and_set_stop_a_run);
  return UNITY_END();
}
