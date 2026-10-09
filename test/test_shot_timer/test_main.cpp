#include <shot_timer.h>
#include <unity.h>

using namespace shot;

static const Config C = {250, 300, 50};

void setUp() {}
void tearDown() {}

// Pulls one shot of durMs starting at t and returns to Ready.
static void pull(ShotTimer &s, uint32_t t, uint32_t durMs) {
  s.tap(t);
  s.tap(t + durMs);
  s.tap(t + durMs + 1);
}

static void test_classify_boundaries() {
  TEST_ASSERT_EQUAL(Zone::Early, classify(0, C));
  TEST_ASSERT_EQUAL(Zone::Early, classify(19999, C));
  TEST_ASSERT_EQUAL(Zone::Close, classify(20000, C));
  TEST_ASSERT_EQUAL(Zone::Close, classify(24999, C));
  TEST_ASSERT_EQUAL(Zone::InTarget, classify(25000, C));
  TEST_ASSERT_EQUAL(Zone::InTarget, classify(30000, C));  // max is inclusive
  TEST_ASSERT_EQUAL(Zone::Over, classify(30001, C));
  TEST_ASSERT_EQUAL(Zone::Over, classify(UINT32_MAX, C));
}

static void test_classify_wide_close_window() {
  const Config wide = {30, 100, 50};  // close window wider than the minimum
  TEST_ASSERT_EQUAL(Zone::Close, classify(0, wide));
  TEST_ASSERT_EQUAL(Zone::InTarget, classify(3000, wide));
  const Config noClose = {250, 300, 0};
  TEST_ASSERT_EQUAL(Zone::Early, classify(24999, noClose));
  TEST_ASSERT_EQUAL(Zone::InTarget, classify(25000, noClose));
}

static void test_mode_cycle_and_elapsed() {
  ShotTimer s(C);
  TEST_ASSERT_EQUAL(Mode::Ready, s.mode());
  TEST_ASSERT_EQUAL_UINT32(0, s.elapsedMs(5000));
  s.tap(1000);
  TEST_ASSERT_EQUAL(Mode::Running, s.mode());
  TEST_ASSERT_EQUAL_UINT32(0, s.elapsedMs(1000));
  TEST_ASSERT_EQUAL_UINT32(18600, s.elapsedMs(19600));  // live
  s.tap(28400);
  TEST_ASSERT_EQUAL(Mode::Done, s.mode());
  TEST_ASSERT_EQUAL_UINT32(27400, s.elapsedMs(99999));  // frozen
  TEST_ASSERT_EQUAL_UINT16(274, s.lastShotDs());
  TEST_ASSERT_EQUAL_UINT8(1, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(274, s.historyAt(0));
  s.tap(30000);
  TEST_ASSERT_EQUAL(Mode::Ready, s.mode());
  TEST_ASSERT_EQUAL_UINT32(0, s.elapsedMs(30000));
  TEST_ASSERT_EQUAL_UINT16(274, s.lastShotDs());  // still shown after reset
}

static void test_rounding_to_tenths() {
  ShotTimer s(C);
  const uint32_t durs[] = {27449, 27450, 27451, 49, 50, 0};
  const uint16_t want[] = {274, 275, 275, 0, 1, 0};
  for (int i = 0; i < 6; i++) {
    s.tap(1000);
    s.tap(1000 + durs[i]);
    TEST_ASSERT_EQUAL_UINT16(want[i], s.lastShotDs());
    s.tap(2000000);
  }
}

static void test_running_across_millis_wrap() {
  ShotTimer s(C);
  s.tap(UINT32_MAX - 999);
  TEST_ASSERT_EQUAL_UINT32(5000, s.elapsedMs(4000));
  s.tap(25000);
  TEST_ASSERT_EQUAL_UINT16(260, s.lastShotDs());
}

static void test_huge_shot_saturates() {
  ShotTimer s(C);
  s.tap(0);
  s.tap(4000000000u);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.lastShotDs());
}

static void test_back() {
  ShotTimer s(C);
  TEST_ASSERT_FALSE(s.back());  // Ready: caller leaves the app
  s.tap(0);
  TEST_ASSERT_FALSE(s.back());  // Running: keeps timing in the background
  TEST_ASSERT_EQUAL(Mode::Running, s.mode());
  s.tap(26000);
  TEST_ASSERT_TRUE(s.back());
  TEST_ASSERT_EQUAL(Mode::Ready, s.mode());
  TEST_ASSERT_EQUAL_UINT8(1, s.historyCount());  // back keeps the shot
}

static void test_discard() {
  ShotTimer s(C);
  pull(s, 0, 26100);
  s.tap(100000);
  s.tap(131800);
  TEST_ASSERT_EQUAL_UINT8(2, s.historyCount());
  s.discard();
  TEST_ASSERT_EQUAL(Mode::Ready, s.mode());
  TEST_ASSERT_EQUAL_UINT8(1, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(261, s.historyAt(0));
  s.discard();  // not in Done: no-op
  TEST_ASSERT_EQUAL_UINT8(1, s.historyCount());
  s.tap(200000);
  s.discard();  // Running: no-op
  TEST_ASSERT_EQUAL(Mode::Running, s.mode());
}

static void test_history_ring_wraparound() {
  ShotTimer s(C);
  for (uint32_t i = 1; i <= 25; i++) pull(s, i * 100000, i * 1000);
  TEST_ASSERT_EQUAL_UINT8(kHistory, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(250, s.historyAt(0));            // newest: shot 25
  TEST_ASSERT_EQUAL_UINT16(60, s.historyAt(kHistory - 1));  // oldest kept: shot 6
  for (uint8_t i = 0; i < kHistory; i++) TEST_ASSERT_EQUAL_UINT16((25 - i) * 10, s.historyAt(i));
  TEST_ASSERT_EQUAL_UINT16(0, s.historyAt(kHistory));  // out of range
}

static void test_discard_when_full() {
  ShotTimer s(C);
  for (uint32_t i = 1; i <= 21; i++) pull(s, i * 100000, i * 1000);
  s.tap(9000000);
  s.tap(9000000 + 99000);
  s.discard();
  TEST_ASSERT_EQUAL_UINT8(kHistory - 1, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(210, s.historyAt(0));
  // The slot the discarded shot overwrote is gone; the ring still works.
  pull(s, 10000000, 42000);
  TEST_ASSERT_EQUAL_UINT8(kHistory, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(420, s.historyAt(0));
  TEST_ASSERT_EQUAL_UINT16(210, s.historyAt(1));
}

static void test_load_history() {
  ShotTimer s(C);
  const uint16_t saved[] = {274, 261, 318, 240};
  s.loadHistory(saved, 4);
  TEST_ASSERT_EQUAL_UINT8(4, s.historyCount());
  for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT16(saved[i], s.historyAt(i));
  pull(s, 0, 25000);
  TEST_ASSERT_EQUAL_UINT8(5, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(250, s.historyAt(0));
  TEST_ASSERT_EQUAL_UINT16(274, s.historyAt(1));

  uint16_t many[30];
  for (uint16_t i = 0; i < 30; i++) many[i] = 100 + i;
  s.loadHistory(many, 30);  // keeps the newest kHistory
  TEST_ASSERT_EQUAL_UINT8(kHistory, s.historyCount());
  TEST_ASSERT_EQUAL_UINT16(100, s.historyAt(0));
  TEST_ASSERT_EQUAL_UINT16(119, s.historyAt(kHistory - 1));

  s.loadHistory(nullptr, 5);
  TEST_ASSERT_EQUAL_UINT8(0, s.historyCount());
}

static void test_config_accessor() {
  ShotTimer s(C);
  TEST_ASSERT_EQUAL_UINT16(250, s.config().targetMinDs);
  TEST_ASSERT_EQUAL_UINT16(300, s.config().targetMaxDs);
  TEST_ASSERT_EQUAL_UINT16(50, s.config().closeWindowDs);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify_boundaries);
  RUN_TEST(test_classify_wide_close_window);
  RUN_TEST(test_mode_cycle_and_elapsed);
  RUN_TEST(test_rounding_to_tenths);
  RUN_TEST(test_running_across_millis_wrap);
  RUN_TEST(test_huge_shot_saturates);
  RUN_TEST(test_back);
  RUN_TEST(test_discard);
  RUN_TEST(test_history_ring_wraparound);
  RUN_TEST(test_discard_when_full);
  RUN_TEST(test_load_history);
  RUN_TEST(test_config_accessor);
  return UNITY_END();
}
