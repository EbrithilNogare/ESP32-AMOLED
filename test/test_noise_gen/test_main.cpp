#include <math.h>
#include <noise_gen.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

using namespace noise;

void setUp() {}
void tearDown() {}

static constexpr size_t kN = 16000 * 5;  // 5 s at 16 kHz
static int16_t buf[kN];

struct Stats {
  double rms, peak, tilt;  // tilt: energy of the difference / energy of a low-passed copy
};

static Stats measure(Kind k, uint32_t seed = 7) {
  Generator g(seed);
  g.fill(k, buf, kN);
  double ss = 0, lo = 0, hi = 0, lp = 0, peak = 0;
  for (size_t i = 0; i < kN; i++) {
    const double x = buf[i] / 32768.0;
    ss += x * x;
    peak = fmax(peak, fabs(x));
    lp += (x - lp) * 0.1;  // one-pole low-pass, ~270 Hz
    lo += lp * lp;
    if (i) {
      const double d = (buf[i] - buf[i - 1]) / 32768.0;
      hi += d * d;
    }
  }
  return {sqrt(ss / kN), peak, hi / lo};
}

static void test_every_kind_is_audible_and_never_clips() {
  for (int k = 0; k < kKinds; k++) {
    const Stats s = measure(Kind(k));
    TEST_ASSERT_TRUE_MESSAGE(s.rms > 0.08 && s.rms < 0.2, name(Kind(k)));
    TEST_ASSERT_TRUE_MESSAGE(s.peak < 0.95, name(Kind(k)));
  }
}

static void test_spectrum_tilts() {
  const Stats rain = measure(Kind::Rain), white = measure(Kind::White), blue = measure(Kind::Blue);
  TEST_ASSERT_TRUE(blue.tilt > 3 * white.tilt);  // blue is brighter than white
  TEST_ASSERT_TRUE(rain.tilt < white.tilt / 3);  // rain is darker
}

static void test_rain_has_drops() {
  // Drops are transients: the peak stands far above the bed, unlike steady noise.
  const Stats rain = measure(Kind::Rain), white = measure(Kind::White);
  TEST_ASSERT_TRUE(rain.peak / rain.rms > 3.5);
  TEST_ASSERT_TRUE(white.peak / white.rms < 2.0);
}

static void test_same_seed_same_sound() {
  static int16_t a[1000], b[1000];
  Generator g1(99), g2(99), g3(100);
  g1.fill(Kind::Rain, a, 1000);
  g2.fill(Kind::Rain, b, 1000);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, 1000);
  g3.fill(Kind::Rain, b, 1000);
  TEST_ASSERT_TRUE(memcmp(a, b, sizeof a) != 0);
}

static void test_zero_seed_still_makes_noise() {
  Generator g(0);  // xorshift would stick at 0
  int16_t s[64];
  g.fill(Kind::White, s, 64);
  int nonzero = 0;
  for (int16_t v : s) nonzero += v != 0;
  TEST_ASSERT_TRUE(nonzero > 32);
}

static void test_next_cycles_both_ways() {
  TEST_ASSERT_EQUAL(Kind::White, next(Kind::Rain, 1));
  TEST_ASSERT_EQUAL(Kind::Blue, next(Kind::White, 1));
  TEST_ASSERT_EQUAL(Kind::Rain, next(Kind::Blue, 1));
  TEST_ASSERT_EQUAL(Kind::Blue, next(Kind::Rain, -1));
  TEST_ASSERT_EQUAL_STRING("Rain", name(Kind::Rain));
  TEST_ASSERT_EQUAL_STRING("Blue noise", name(Kind::Blue));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_kind_is_audible_and_never_clips);
  RUN_TEST(test_spectrum_tilts);
  RUN_TEST(test_rain_has_drops);
  RUN_TEST(test_same_seed_same_sound);
  RUN_TEST(test_zero_seed_still_makes_noise);
  RUN_TEST(test_next_cycles_both_ways);
  return UNITY_END();
}
