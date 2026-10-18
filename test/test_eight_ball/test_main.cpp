#include <eight_ball.h>
#include <math.h>
#include <string.h>
#include <unity.h>

using namespace eightball;

void setUp() {}
void tearDown() {}

static const ShakeConfig C = {0.6f, 3, 800, 500};

static void test_answers() {
  int yes = 0, maybe = 0, no = 0;
  for (int i = 0; i < kAnswers; i++) {
    const Answer &a = answer(i);
    TEST_ASSERT_NOT_NULL(a.text);
    TEST_ASSERT_TRUE(strlen(a.text) > 0);
    for (const char *p = a.text; *p; p++) TEST_ASSERT_FALSE(*p >= 'a' && *p <= 'z');  // capitals only (the font)
    yes += a.kind == Kind::Yes;
    maybe += a.kind == Kind::Maybe;
    no += a.kind == Kind::No;
  }
  TEST_ASSERT_EQUAL_INT(10, yes);
  TEST_ASSERT_EQUAL_INT(5, maybe);
  TEST_ASSERT_EQUAL_INT(5, no);
  TEST_ASSERT_EQUAL_PTR(answer(0).text, answer(99).text);  // out of range: the first
}

static void test_pick_never_repeats_and_covers_all() {
  bool seen[kAnswers] = {};
  for (uint32_t r = 0; r < 200; r++) {
    const int a = pick(r * 2654435761u, 7);
    TEST_ASSERT_TRUE(a >= 0 && a < kAnswers);
    TEST_ASSERT_NOT_EQUAL(7, a);
    seen[a] = true;
  }
  for (int i = 0; i < kAnswers; i++)
    if (i != 7) TEST_ASSERT_TRUE(seen[i]);
  TEST_ASSERT_EQUAL_INT(19, pick(19, -1));
  TEST_ASSERT_EQUAL_INT(0, pick(20, -1));
}

// Sample at 100 Hz: still (1 g on z) except for jolts at the given ms.
static void feed(Shake &s, uint32_t from, uint32_t to, float g, Shake::Event *last) {
  for (uint32_t t = from; t < to; t += 10) {
    const Shake::Event e = s.update(0, 0, g, t);
    if (e != Shake::Event::None) *last = e;
  }
}

static void test_three_jolts_start_then_settle() {
  Shake s(C);
  Shake::Event ev = Shake::Event::None;
  feed(s, 0, 200, 1.0f, &ev);
  TEST_ASSERT_FALSE(s.shaking());
  for (int j = 0; j < 3; j++) {
    feed(s, 200 + j * 200, 250 + j * 200, 2.0f, &ev);  // 50 ms at 2 g
    feed(s, 250 + j * 200, 400 + j * 200, 1.0f, &ev);
  }
  TEST_ASSERT_TRUE(s.shaking());
  TEST_ASSERT_EQUAL(Shake::Event::Started, ev);
  feed(s, 800, 1100, 1.0f, &ev);  // the last jolt sample was at 640: 450 ms calm, not yet
  TEST_ASSERT_TRUE(s.shaking());
  feed(s, 1100, 1300, 1.0f, &ev);
  TEST_ASSERT_FALSE(s.shaking());
  TEST_ASSERT_EQUAL(Shake::Event::Settled, ev);
}

static void test_one_long_jolt_is_one() {
  Shake s(C);
  Shake::Event ev = Shake::Event::None;
  feed(s, 0, 600, 2.0f, &ev);  // held at 2 g (a hard tilt / drop): one jolt only
  TEST_ASSERT_FALSE(s.shaking());
  TEST_ASSERT_EQUAL(Shake::Event::None, ev);
}

static void test_slow_jolts_are_not_a_shake() {
  Shake s(C);
  Shake::Event ev = Shake::Event::None;
  for (int j = 0; j < 4; j++) {  // a jolt every 600 ms: 3 never fit in 800 ms
    feed(s, j * 600, j * 600 + 30, 1.8f, &ev);
    feed(s, j * 600 + 30, j * 600 + 600, 1.0f, &ev);
  }
  TEST_ASSERT_FALSE(s.shaking());
}

static void test_tilt_is_not_a_jolt() {
  Shake s(C);
  Shake::Event ev = Shake::Event::None;
  // Turning the device: the direction changes, |a| stays ~1 g.
  for (uint32_t t = 0; t < 2000; t += 10) {
    const float a = t / 2000.0f * 3.14159f;
    const Shake::Event e = s.update(sinf(a), 0, cosf(a), t);
    if (e != Shake::Event::None) ev = e;
  }
  TEST_ASSERT_FALSE(s.shaking());
  TEST_ASSERT_EQUAL(Shake::Event::None, ev);
}

static void test_free_fall_counts() {  // |a| near 0 is as far from 1 g as 2 g
  Shake s(C);
  Shake::Event ev = Shake::Event::None;
  for (int j = 0; j < 3; j++) {
    feed(s, j * 150, j * 150 + 40, 0.1f, &ev);
    feed(s, j * 150 + 40, j * 150 + 150, 1.0f, &ev);
  }
  TEST_ASSERT_TRUE(s.shaking());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_answers);
  RUN_TEST(test_pick_never_repeats_and_covers_all);
  RUN_TEST(test_three_jolts_start_then_settle);
  RUN_TEST(test_one_long_jolt_is_one);
  RUN_TEST(test_slow_jolts_are_not_a_shake);
  RUN_TEST(test_tilt_is_not_a_jolt);
  RUN_TEST(test_free_fall_counts);
  return UNITY_END();
}
