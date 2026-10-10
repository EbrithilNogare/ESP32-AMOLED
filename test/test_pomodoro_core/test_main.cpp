#include <pomodoro_core.h>
#include <unity.h>

using namespace pomo;

static const uint32_t MIN = 60000;
static const Config AUTO = {25 * MIN, 5 * MIN, 15 * MIN, 4, true};
static const Config MANUAL = {25 * MIN, 5 * MIN, 15 * MIN, 4, false};

void setUp() {}
void tearDown() {}

static void test_initial_idle() {
  Pomodoro p(AUTO);
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.phaseDurationMs());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(123456));
  TEST_ASSERT_EQUAL_UINT8(0, p.completed());
  TEST_ASSERT_EQUAL(Phase::Focus, p.nextPhase());
  TEST_ASSERT_FALSE(p.update(1000000000ULL));  // nothing runs
}

static void test_start_pause_resume() {
  Pomodoro p(AUTO);
  p.toggle(1000);
  TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
  TEST_ASSERT_TRUE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(1000));
  TEST_ASSERT_EQUAL_UINT32(25 * MIN - 5000, p.remainingMs(6000));
  p.toggle(6000);  // pause
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN - 5000, p.remainingMs(999999));  // frozen
  TEST_ASSERT_FALSE(p.update(100 * MIN));                            // paused: never ends
  p.toggle(100 * MIN);                                               // resume
  TEST_ASSERT_TRUE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN - 5000, p.remainingMs(100 * MIN));
  p.pause(100 * MIN + 1000);
  TEST_ASSERT_EQUAL_UINT32(25 * MIN - 6000, p.remainingMs(0));
  p.pause(100 * MIN + 9000);  // already paused: no change
  TEST_ASSERT_EQUAL_UINT32(25 * MIN - 6000, p.remainingMs(0));
}

static void test_focus_ends_exactly_at_end_timestamp() {
  Pomodoro p(MANUAL);
  p.toggle(0);
  TEST_ASSERT_FALSE(p.update(25 * MIN - 1));
  TEST_ASSERT_EQUAL_UINT32(1, p.remainingMs(25 * MIN - 1));
  TEST_ASSERT_TRUE(p.update(25 * MIN));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT8(1, p.completed());
  TEST_ASSERT_FALSE(p.running());  // manual: waits
  TEST_ASSERT_EQUAL_UINT32(5 * MIN, p.remainingMs(30 * MIN));
  TEST_ASSERT_FALSE(p.update(60 * MIN));
}

static void test_full_set_auto() {
  Pomodoro p(AUTO);
  uint64_t t = 0;
  p.toggle(t);
  for (int i = 1; i <= 4; i++) {
    TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
    TEST_ASSERT_EQUAL(i == 4 ? Phase::LongBreak : Phase::ShortBreak, p.nextPhase());
    t += 25 * MIN;
    TEST_ASSERT_TRUE(p.update(t));
    TEST_ASSERT_EQUAL_UINT8(i, p.completed());
    TEST_ASSERT_TRUE(p.running());
    if (i < 4) {
      TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
      TEST_ASSERT_EQUAL(Phase::Focus, p.nextPhase());
      t += 5 * MIN;
      TEST_ASSERT_TRUE(p.update(t));
    }
  }
  TEST_ASSERT_EQUAL(Phase::LongBreak, p.phase());
  TEST_ASSERT_EQUAL(Phase::Idle, p.nextPhase());
  TEST_ASSERT_EQUAL_UINT32(15 * MIN, p.remainingMs(t));
  t += 15 * MIN;
  TEST_ASSERT_TRUE(p.update(t));
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());  // set complete: Idle waits even with auto
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT8(0, p.completed());
}

static void test_auto_start_carries_overshoot() {
  Pomodoro p(AUTO);
  p.toggle(0);
  // update() runs 3 s late: the break started at 25:00, not at 25:03.
  TEST_ASSERT_TRUE(p.update(25 * MIN + 3000));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT32(5 * MIN - 3000, p.remainingMs(25 * MIN + 3000));
}

static void test_catch_up_over_many_phases() {
  Pomodoro p(AUTO);
  p.toggle(0);
  // Focus 25, Short 5, Focus 25 → 55 min; 10 s into the 2nd short break.
  TEST_ASSERT_TRUE(p.update(55 * MIN + 10000));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT8(2, p.completed());
  TEST_ASSERT_EQUAL_UINT32(5 * MIN - 10000, p.remainingMs(55 * MIN + 10000));
  // Far beyond the whole set: ends in Idle, completed reset.
  TEST_ASSERT_TRUE(p.update(1000 * MIN));
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  TEST_ASSERT_EQUAL_UINT8(0, p.completed());
  TEST_ASSERT_FALSE(p.running());
}

static void test_manual_late_update_ends_one_phase() {
  Pomodoro p(MANUAL);
  p.toggle(0);
  TEST_ASSERT_TRUE(p.update(500 * MIN));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT32(5 * MIN, p.remainingMs(500 * MIN));
  p.toggle(500 * MIN);
  TEST_ASSERT_TRUE(p.update(505 * MIN));
  TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
  TEST_ASSERT_FALSE(p.running());
}

static void test_remaining_never_underflows() {
  Pomodoro p(MANUAL);
  p.toggle(1000);
  TEST_ASSERT_EQUAL_UINT32(0, p.remainingMs(25 * MIN + 1000));
  TEST_ASSERT_EQUAL_UINT32(0, p.remainingMs(1000 * MIN));
  p.pause(1000 * MIN);  // paused after the end, before update(): 0, not wrapped
  TEST_ASSERT_EQUAL_UINT32(0, p.remainingMs(1000 * MIN));
}

static void test_reset() {
  Pomodoro p(AUTO);
  p.reset(0);  // Idle stays Idle
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  p.toggle(0);
  p.reset(10 * MIN);
  TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(20 * MIN));
  p.toggle(20 * MIN);  // "Start" again from full
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(20 * MIN));
  // In a break: back to the full break, completed unchanged.
  TEST_ASSERT_TRUE(p.update(45 * MIN));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  p.reset(47 * MIN);
  TEST_ASSERT_EQUAL_UINT32(5 * MIN, p.remainingMs(47 * MIN));
  TEST_ASSERT_EQUAL_UINT8(1, p.completed());
}

static void test_skip() {
  Pomodoro p(AUTO);
  p.skip(0);  // Idle: no-op
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  p.toggle(0);
  p.skip(60000);  // counts like a normal end, next runs from now
  TEST_ASSERT_EQUAL(Phase::ShortBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT8(1, p.completed());
  TEST_ASSERT_TRUE(p.running());
  TEST_ASSERT_EQUAL_UINT32(5 * MIN, p.remainingMs(60000));
  // Skipping a paused phase leaves the next one paused.
  p.pause(70000);
  p.skip(80000);
  TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(80000));
}

static void test_skip_through_set_to_idle() {
  Pomodoro p(MANUAL);
  p.toggle(0);
  for (int i = 0; i < 7; i++) p.skip(1000);  // F S F S F S F
  TEST_ASSERT_EQUAL(Phase::LongBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT8(4, p.completed());
  TEST_ASSERT_FALSE(p.running());  // manual
  p.skip(2000);
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  TEST_ASSERT_EQUAL_UINT8(0, p.completed());
}

static void test_set_size_one_and_zero() {
  Pomodoro one({1000, 500, 2000, 1, true});
  one.toggle(0);
  TEST_ASSERT_EQUAL(Phase::LongBreak, one.nextPhase());
  TEST_ASSERT_TRUE(one.update(1000));
  TEST_ASSERT_EQUAL(Phase::LongBreak, one.phase());
  Pomodoro zero({1000, 500, 2000, 0, true});  // treated as 1, no division by zero
  zero.toggle(0);
  TEST_ASSERT_TRUE(zero.update(1000));
  TEST_ASSERT_EQUAL(Phase::LongBreak, zero.phase());
}

static void test_zero_durations_terminate() {
  Pomodoro p({0, 0, 0, 4, true});
  p.toggle(5);
  TEST_ASSERT_TRUE(p.update(5));
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
}

static void test_save_restore_paused() {
  Pomodoro a(AUTO);
  a.toggle(0);
  TEST_ASSERT_TRUE(a.update(26 * MIN));  // focus done → short break, 4 min left
  a.toggle(27 * MIN);                    // pause, 3 min left
  a.toggle(28 * MIN);                    // resume
  a.pause(29 * MIN);                     // 2 min left
  const Saved s = a.save(29 * MIN);
  TEST_ASSERT_EQUAL_UINT32(2 * MIN, s.remainingMs);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Phase::ShortBreak), s.phase);
  TEST_ASSERT_EQUAL_UINT8(0, s.running);
  TEST_ASSERT_EQUAL_UINT8(1, s.completed);

  Pomodoro b(AUTO);
  b.restore(s, 5);  // a fresh boot clock
  TEST_ASSERT_EQUAL(Phase::ShortBreak, b.phase());
  TEST_ASSERT_FALSE(b.running());
  TEST_ASSERT_EQUAL_UINT8(1, b.completed());
  TEST_ASSERT_EQUAL_UINT32(2 * MIN, b.remainingMs(999999));
}

static void test_save_restore_running() {
  Pomodoro a(AUTO);
  a.toggle(1000);
  const Saved s = a.save(1000 + 10 * MIN);
  TEST_ASSERT_EQUAL_UINT8(1, s.running);
  TEST_ASSERT_EQUAL_UINT32(15 * MIN, s.remainingMs);
  Pomodoro b(AUTO);
  b.restore(s, 50);
  TEST_ASSERT_TRUE(b.running());
  TEST_ASSERT_EQUAL_UINT32(15 * MIN, b.remainingMs(50));
  TEST_ASSERT_FALSE(b.update(50 + 15 * MIN - 1));
  TEST_ASSERT_TRUE(b.update(50 + 15 * MIN));
  TEST_ASSERT_EQUAL(Phase::ShortBreak, b.phase());
}

static void test_restore_clamps_garbage() {
  Pomodoro p(AUTO);
  p.restore({9, 1, 200, 12345}, 0);  // bad phase → Idle
  TEST_ASSERT_EQUAL(Phase::Idle, p.phase());
  TEST_ASSERT_FALSE(p.running());
  TEST_ASSERT_EQUAL_UINT8(0, p.completed());
  p.restore({uint8_t(Phase::Focus), 0, 200, 99 * MIN}, 0);
  TEST_ASSERT_EQUAL(Phase::Focus, p.phase());
  TEST_ASSERT_EQUAL_UINT8(3, p.completed());             // Focus holds at most setSize - 1
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(0));  // clamped to the phase
  // Ending that focus completes the set exactly: long break with setSize done.
  p.skip(0);
  TEST_ASSERT_EQUAL(Phase::LongBreak, p.phase());
  TEST_ASSERT_EQUAL_UINT8(4, p.completed());
  p.restore({uint8_t(Phase::ShortBreak), 0, 4, MIN}, 0);
  TEST_ASSERT_EQUAL_UINT8(3, p.completed());
  p.restore({uint8_t(Phase::LongBreak), 0, 200, MIN}, 0);
  TEST_ASSERT_EQUAL_UINT8(4, p.completed());  // LongBreak may hold setSize
}

static void test_large_timestamps() {
  Pomodoro p(AUTO);
  const uint64_t t = 0xFFFFFFFFULL * 3;  // past any 32-bit millis() wrap
  p.toggle(t);
  TEST_ASSERT_EQUAL_UINT32(25 * MIN, p.remainingMs(t));
  TEST_ASSERT_TRUE(p.update(t + 25 * MIN));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_initial_idle);
  RUN_TEST(test_start_pause_resume);
  RUN_TEST(test_focus_ends_exactly_at_end_timestamp);
  RUN_TEST(test_full_set_auto);
  RUN_TEST(test_auto_start_carries_overshoot);
  RUN_TEST(test_catch_up_over_many_phases);
  RUN_TEST(test_manual_late_update_ends_one_phase);
  RUN_TEST(test_remaining_never_underflows);
  RUN_TEST(test_reset);
  RUN_TEST(test_skip);
  RUN_TEST(test_skip_through_set_to_idle);
  RUN_TEST(test_set_size_one_and_zero);
  RUN_TEST(test_zero_durations_terminate);
  RUN_TEST(test_save_restore_paused);
  RUN_TEST(test_save_restore_running);
  RUN_TEST(test_restore_clamps_garbage);
  RUN_TEST(test_large_timestamps);
  return UNITY_END();
}
