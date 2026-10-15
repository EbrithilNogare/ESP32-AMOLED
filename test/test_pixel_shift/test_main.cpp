#include <pixel_shift.h>
#include <stdlib.h>
#include <unity.h>

using namespace burnin;

static const uint32_t N = 20000;  // ~42 days at one step per 3 min

void setUp() {}
void tearDown() {}

static void test_step_zero_is_origin() {
  const int8_t sizes[] = {0, 1, 4, 10, 127};
  for (int8_t m : sizes) {
    const Offset o = pixelShift(0, m);
    TEST_ASSERT_EQUAL_INT8(0, o.x);
    TEST_ASSERT_EQUAL_INT8(0, o.y);
  }
}

static void test_bounded_and_uses_the_range() {
  const int8_t sizes[] = {1, 2, 4, 8, 127};
  for (int8_t m : sizes) {
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (uint32_t s = 0; s < N; s++) {
      const Offset o = pixelShift(s, m);
      TEST_ASSERT_TRUE(abs(o.x) <= m && abs(o.y) <= m);
      if (o.x < minX) minX = o.x;
      if (o.x > maxX) maxX = o.x;
      if (o.y < minY) minY = o.y;
      if (o.y > maxY) maxY = o.y;
    }
    TEST_ASSERT_EQUAL_INT(-m, minX);
    TEST_ASSERT_EQUAL_INT(m, maxX);
    TEST_ASSERT_EQUAL_INT(-m, minY);
    TEST_ASSERT_EQUAL_INT(m, maxY);
  }
}

static void test_small_steps() {
  const int8_t sizes[] = {1, 4, 10, 127};
  for (int8_t m : sizes) {
    Offset prev = pixelShift(0, m);
    for (uint32_t s = 1; s < N; s++) {
      const Offset o = pixelShift(s, m);
      TEST_ASSERT_TRUE(abs(o.x - prev.x) <= 2 && abs(o.y - prev.y) <= 2);
      prev = o;
    }
  }
}

static void test_moves_most_steps() {
  // A shift that rarely moves wouldn't protect anything.
  uint32_t still = 0;
  Offset prev = pixelShift(0, 4);
  for (uint32_t s = 1; s < N; s++) {
    const Offset o = pixelShift(s, 4);
    if (o.x == prev.x && o.y == prev.y) still++;
    prev = o;
  }
  TEST_ASSERT_LESS_THAN_UINT32(N / 3, still);
}

static void test_no_short_period() {
  // No period p up to 1000 steps (~50 h) reproduces the path.
  static Offset path[3000];
  for (uint32_t s = 0; s < 3000; s++) path[s] = pixelShift(s, 4);
  for (uint32_t p = 1; p <= 1000; p++) {
    bool same = true;
    for (uint32_t i = 0; i + p < 3000 && same; i++) {
      same = path[i].x == path[i + p].x && path[i].y == path[i + p].y;
    }
    TEST_ASSERT_FALSE_MESSAGE(same, "path repeats with a short period");
  }
}

static void test_visits_every_spot() {
  // The Lissajous path covers the whole square, so wear spreads evenly-ish.
  bool seen[9][9] = {};
  for (uint32_t s = 0; s < N; s++) {
    const Offset o = pixelShift(s, 4);
    seen[o.x + 4][o.y + 4] = true;
  }
  int count = 0;
  for (auto &row : seen)
    for (bool b : row) count += b;
  TEST_ASSERT_EQUAL_INT(81, count);
}

static void test_deterministic_and_large_steps() {
  // Pinned values (from the implementation at the time of writing): the path
  // must not change between builds, or a restored step would jump.
  struct Case {
    uint32_t step;
    int8_t maxPx, x, y;
  };
  const Case cases[] = {
      {1, 4, 1, 1},
      {2, 4, 2, 1},
      {7, 4, 3, 4},
      {100, 4, -4, -1},
      {123456, 4, -2, 1},
      {1000000, 4, 0, -1},
      {4000000000u, 4, -4, 4},
      {7, 10, 7, 5},
      {100, 10, -5, 9},
      {123456, 10, -9, 10},
      {4000000000u, 10, -9, -7},
  };
  for (const Case &c : cases) {
    const Offset o = pixelShift(c.step, c.maxPx);
    TEST_ASSERT_EQUAL_INT8(c.x, o.x);
    TEST_ASSERT_EQUAL_INT8(c.y, o.y);
  }
  Offset prev = pixelShift(UINT32_MAX - 100, 4);
  for (uint32_t s = UINT32_MAX - 99; s != 0; s++) {  // up to the wrap
    const Offset o = pixelShift(s, 4);
    TEST_ASSERT_TRUE(abs(o.x) <= 4 && abs(o.y) <= 4);
    TEST_ASSERT_TRUE(abs(o.x - prev.x) <= 2 && abs(o.y - prev.y) <= 2);
    prev = o;
  }
}

static void test_non_positive_max_is_still() {
  for (uint32_t s = 0; s < 100; s++) {
    TEST_ASSERT_EQUAL_INT8(0, pixelShift(s, 0).x);
    TEST_ASSERT_EQUAL_INT8(0, pixelShift(s, -3).y);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_zero_is_origin);
  RUN_TEST(test_bounded_and_uses_the_range);
  RUN_TEST(test_small_steps);
  RUN_TEST(test_moves_most_steps);
  RUN_TEST(test_no_short_period);
  RUN_TEST(test_visits_every_spot);
  RUN_TEST(test_deterministic_and_large_steps);
  RUN_TEST(test_non_positive_max_is_still);
  return UNITY_END();
}
