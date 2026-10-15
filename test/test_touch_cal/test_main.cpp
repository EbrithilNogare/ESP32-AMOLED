#include <touch_cal.h>
#include <unity.h>

using namespace touchcal;

void setUp() {}
void tearDown() {}

// The five targets the calibration screen shows (368 × 448 panel).
static const int16_t kTx[5] = {40, 328, 328, 40, 184}, kTy[5] = {40, 40, 408, 408, 224};

// Samples from a panel that reports raw = (screen - off) / scale.
static void make(Sample *s, double sx, double ox, double sy, double oy) {
  for (int i = 0; i < 5; i++)
    s[i] = {int16_t((kTx[i] - ox) / sx + 0.5), int16_t((kTy[i] - oy) / sy + 0.5), kTx[i], kTy[i]};
}

static void test_identity_maps_unchanged() {
  const Cal c;
  TEST_ASSERT_EQUAL_INT16(0, mapX(c, 0));
  TEST_ASSERT_EQUAL_INT16(367, mapX(c, 367));
  TEST_ASSERT_EQUAL_INT16(447, mapY(c, 447));
  TEST_ASSERT_TRUE(plausible(c));
}

static void test_fit_recovers_scale_and_offset() {
  Sample s[5];
  make(s, 0.92, 15, 1.06, -12);  // the panel reads 8 % wide on x, 6 % short on y
  Cal c;
  TEST_ASSERT_TRUE(fit(s, 5, 6, c));
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_INT16_WITHIN(1, kTx[i], mapX(c, s[i].rawX));
    TEST_ASSERT_INT16_WITHIN(1, kTy[i], mapY(c, s[i].rawY));
  }
  TEST_ASSERT_INT32_WITHIN(400, 60293, c.xScale);  // 0.92 × 65536
  TEST_ASSERT_INT32_WITHIN(1, 15, c.xOffset);
}

static void test_fit_of_a_perfect_panel_is_identity() {
  Sample s[5];
  make(s, 1, 0, 1, 0);
  Cal c;
  TEST_ASSERT_TRUE(fit(s, 5, 6, c));
  TEST_ASSERT_EQUAL_INT32(65536, c.xScale);
  TEST_ASSERT_EQUAL_INT32(0, c.xOffset);
  TEST_ASSERT_EQUAL_INT32(65536, c.yScale);
  TEST_ASSERT_EQUAL_INT32(0, c.yOffset);
}

static void test_sloppy_tap_is_rejected() {
  Sample s[5];
  make(s, 1, 0, 1, 0);
  s[4].rawX += 40;  // the centre tap landed 40 px off
  Cal c;
  TEST_ASSERT_FALSE(fit(s, 5, 20, c));
}

static void test_implausible_correction_is_rejected() {
  Sample s[5];
  make(s, 0.5, 0, 1, 0);  // half the width: not a panel error
  Cal c;
  TEST_ASSERT_FALSE(fit(s, 5, 20, c));
  make(s, 1, 100, 1, 0);  // 100 px off
  TEST_ASSERT_FALSE(fit(s, 5, 20, c));
}

static void test_degenerate_samples_are_rejected() {
  Sample s[3] = {{100, 10, 100, 10}, {100, 200, 100, 200}, {100, 400, 100, 400}};  // one x
  Cal c;
  TEST_ASSERT_FALSE(fit(s, 3, 20, c));
  TEST_ASSERT_FALSE(fit(s, 1, 20, c));
}

static void test_map_clamps_to_int16() {
  Cal c;
  c.xScale = 65536 * 12 / 10;
  c.xOffset = 64;
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, mapX(c, INT16_MAX));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identity_maps_unchanged);
  RUN_TEST(test_fit_recovers_scale_and_offset);
  RUN_TEST(test_fit_of_a_perfect_panel_is_identity);
  RUN_TEST(test_sloppy_tap_is_rejected);
  RUN_TEST(test_implausible_correction_is_rejected);
  RUN_TEST(test_degenerate_samples_are_rejected);
  RUN_TEST(test_map_clamps_to_int16);
  return UNITY_END();
}
