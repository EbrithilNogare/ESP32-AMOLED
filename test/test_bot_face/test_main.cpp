#include <bot_face.h>
#include <math.h>
#include <unity.h>

using namespace bot;

static const Params REST = {1.0f, 0.0f, 0.0f, 0.55f, 0.0f, 0.0f, 1.0f};
static const float EPS = 1e-3f;

void setUp() {}
void tearDown() {}

static void assertRect(float x, float y, float w, float h, float r, const Rect &got) {
  TEST_ASSERT_FLOAT_WITHIN(EPS, x, got.x);
  TEST_ASSERT_FLOAT_WITHIN(EPS, y, got.y);
  TEST_ASSERT_FLOAT_WITHIN(EPS, w, got.w);
  TEST_ASSERT_FLOAT_WITHIN(EPS, h, got.h);
  TEST_ASSERT_FLOAT_WITHIN(EPS, r, got.r);
}

// Mirror of the face's xorshift32, to predict its random draws.
static float nextRand(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return float(s >> 8) * (1.0f / 16777216.0f);
}

// ---- geometry: the mockup numbers ----

static void test_rest_geometry_matches_mockup() {
  const Geometry g = computeGeometry(REST, 1.0f, 0.0f);
  // Eyes 86 x 118, radius 28, centres (124, 196) and (244, 196).
  assertRect(124 - 43, 196 - 59, 86, 118, 28, g.eyeL);
  assertRect(244 - 43, 196 - 59, 86, 118, 28, g.eyeR);
  TEST_ASSERT_FALSE(g.mouthIsO);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 184, g.mx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 318, g.my);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.55f, g.curve);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0, g.oRx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0, g.oRy);
}

static void test_face_at_start_is_rest_geometry() {
  // At t = start: breathe = sin(0) = 0, no blink, no look yet.
  const Face f(7, 5000);
  const Geometry g = f.geometry(5000);
  assertRect(81, 137, 86, 118, 28, g.eyeL);
  assertRect(201, 137, 86, 118, 28, g.eyeR);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 184, g.mx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 318, g.my);
}

static void test_look_bounce_width_formulas() {
  Params p = REST;
  p.lookX = 1;
  p.lookY = -0.5f;
  p.bounce = -10;
  p.width = 1.15f;
  const Geometry g = computeGeometry(p, 1.0f, 2.0f);
  const float w = 86 * 1.15f;
  const float cy = 196 + 14 * -0.5f + 2 - 10;
  assertRect(124 + 18 - w / 2, cy - 59, w, 118, 28, g.eyeL);
  assertRect(244 + 18 - w / 2, cy - 59, w, 118, 28, g.eyeR);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 184 + 8, g.mx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 318 + 2 - 6, g.my);  // 0.6 * bounce
}

static void test_closed_eye_is_pill() {
  Params p = REST;
  p.open = 0;
  const Geometry g = computeGeometry(p, 1.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 8, g.eyeL.h);  // min height
  TEST_ASSERT_FLOAT_WITHIN(EPS, 4, g.eyeL.r);  // h / 2
  TEST_ASSERT_FLOAT_WITHIN(EPS, 196 - 4, g.eyeL.y);
  p.open = 0.3f;  // squint: 35.4 px tall, so the radius is h / 2
  const Geometry s = computeGeometry(p, 1.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 35.4f, s.eyeL.h);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 17.7f, s.eyeL.r);
}

static void test_o_mouth_threshold_and_size() {
  Params p = REST;
  p.mouthO = 0.15f;
  TEST_ASSERT_FALSE(computeGeometry(p, 1, 0).mouthIsO);
  p.mouthO = 0.16f;
  TEST_ASSERT_TRUE(computeGeometry(p, 1, 0).mouthIsO);
  p.mouthO = 1;
  p.lookX = -1;
  const Geometry g = computeGeometry(p, 1, 1);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 176, g.oCx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 318 + 1 + 6, g.oCy);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 22, g.oRx);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 24, g.oRy);
}

static void test_breathing() {
  const Face f(1, 0);
  // breathe = 3 sin(t / 900): peak at 900·π/2 ≈ 1414 ms, zero at 900·π ≈ 2827 ms.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 196 + 3, f.geometry(1414).eyeL.y + 59);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 318 + 3, f.geometry(1414).my);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 196, f.geometry(2827).eyeL.y + 59);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 196 - 3, f.geometry(4241).eyeL.y + 59);
  // The internal wrap at 15 periods doesn't jump.
  const float a = f.geometry(84822).my, b = f.geometry(84823).my, c = f.geometry(84824).my;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2 * b, a + c);
  // Still smooth after days of uptime.
  const uint32_t day3 = 3u * 86400000u;
  const float d0 = f.geometry(day3).my, d1 = f.geometry(day3 + 1).my;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, d0, d1);
}

// ---- scheduling ----

static void test_first_blink_after_1500_ms() {
  Face f(3, 1000);
  f.update(2500, false);  // exactly start + 1500: not yet (strict, as in the mockup)
  TEST_ASSERT_FLOAT_WITHIN(EPS, 118, f.geometry(2575).eyeL.h);
  f.update(2501, false);                                        // blink starts here
  TEST_ASSERT_FLOAT_WITHIN(EPS, 118, f.geometry(2501).eyeL.h);  // phase 0
  // Mid-blink: 1 - 0.93 = 7 % of 118 = 8.26 px.
  TEST_ASSERT_FLOAT_WITHIN(EPS, 118 * 0.07f, f.geometry(2576).eyeL.h);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 118, f.geometry(2651).eyeL.h);  // 150 ms: open again
}

static void test_first_look_after_1200_ms_uses_seeded_draws() {
  const uint32_t seed = 12345;
  Face f(seed, 0);
  f.update(1200, false);
  TEST_ASSERT_EQUAL_FLOAT(0, f.targets().lookX);
  TEST_ASSERT_EQUAL_FLOAT(0, f.targets().lookY);
  f.update(1201, false);
  uint32_t s = seed;
  const bool centre = nextRand(s) < 0.3f;
  const float x = centre ? 0 : nextRand(s) * 2 - 1;
  const float y = centre ? 0 : nextRand(s) * 1.2f - 0.6f;
  TEST_ASSERT_EQUAL_FLOAT(x, f.targets().lookX);
  TEST_ASSERT_EQUAL_FLOAT(y, f.targets().lookY);
}

static void test_deterministic_for_seed() {
  Face a(42, 0), b(42, 0), c(43, 0);
  bool differs = false;
  for (uint32_t t = 0; t < 30000; t += 33) {
    a.update(t, false);
    b.update(t, false);
    c.update(t, false);
    if (t == 9999 || t == 20031) {
      a.tap(t);
      b.tap(t);
      c.tap(t);
    }
    const Geometry ga = a.geometry(t), gb = b.geometry(t), gc = c.geometry(t);
    TEST_ASSERT_EQUAL_FLOAT(ga.eyeL.x, gb.eyeL.x);
    TEST_ASSERT_EQUAL_FLOAT(ga.eyeL.h, gb.eyeL.h);
    TEST_ASSERT_EQUAL_FLOAT(ga.my, gb.my);
    TEST_ASSERT_EQUAL_FLOAT(a.params().lookY, b.params().lookY);
    if (ga.eyeL.x != gc.eyeL.x || ga.eyeL.h != gc.eyeL.h) differs = true;
  }
  TEST_ASSERT_TRUE(differs);
}

// Runs a face for durMs in 2 ms steps and collects blink start times.
static int collectBlinks(Face &f, uint32_t durMs, bool dimmed, uint32_t *starts, int cap) {
  int n = 0;
  bool inBlink = false;
  for (uint32_t t = 0; t < durMs; t += 2) {
    f.update(t, dimmed);
    // Eyes fully eased to open (no taps), so any dip below open height is a blink.
    const float openH = 118 * f.params().open;
    const bool dip = f.geometry(t).eyeL.h < openH - 0.5f;
    if (dip && !inBlink && n < cap) starts[n++] = t;
    inBlink = dip;
  }
  return n;
}

static void checkBlinkIntervals(bool dimmed) {
  static uint32_t starts[400];
  Face f(99, 0);
  const float m = dimmed ? 1.8f : 1.0f;
  const int n = collectBlinks(f, 900000, dimmed, starts, 400);  // 15 min
  TEST_ASSERT_GREATER_THAN(100, n);
  int doubles = 0;
  for (int i = 1; i < n; i++) {
    const float gap = float(starts[i] - starts[i - 1]);
    if (gap < 1000) {
      doubles++;
      TEST_ASSERT_FLOAT_WITHIN(6, 260 * m, gap);
    } else {
      TEST_ASSERT_TRUE(gap >= 2000 * m - 6 && gap <= 6000 * m + 6);
    }
  }
  // About 20 % double blinks.
  const float ratio = float(doubles) / float(n - 1);
  TEST_ASSERT_TRUE(ratio > 0.08f && ratio < 0.35f);
}

static void test_blink_intervals() { checkBlinkIntervals(false); }
static void test_blink_intervals_dimmed_are_slower() { checkBlinkIntervals(true); }

static void test_look_targets_range_and_timing() {
  Face f(2024, 0);
  float lastX = 0, lastY = 0;
  uint32_t lastChange = 0;
  int changes = 0, centres = 0;
  for (uint32_t t = 0; t < 900000; t += 5) {
    f.update(t, false);
    const float x = f.targets().lookX, y = f.targets().lookY;
    TEST_ASSERT_TRUE(x >= -1 && x <= 1);
    TEST_ASSERT_TRUE(y >= -0.6f && y <= 0.6f);
    if (x != lastX || y != lastY) {
      if (changes > 0) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1400, t - lastChange);
      if (x == 0 && y == 0) centres++;
      changes++;
      lastChange = t;
      lastX = x;
      lastY = y;
    }
    TEST_ASSERT_TRUE(fabsf(f.params().lookX) <= 1.0f);
  }
  // A look every 1.4-3.6 s (2.5 s mean) → ~360 in 15 min, minus centre→centre repeats.
  TEST_ASSERT_TRUE(changes > 200 && changes < 650);
  TEST_ASSERT_TRUE(centres > changes / 10 && centres < changes / 2);
}

// ---- expressions ----

static void test_happy_tap_and_hop() {
  Face f(5, 0);
  f.update(100, false);
  f.tap(100);
  f.update(110, false);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().curve);
  TEST_ASSERT_EQUAL_FLOAT(1.08f, f.targets().width);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, f.targets().mouthO);
  TEST_ASSERT_EQUAL_FLOAT(-14.0f, f.targets().bounce);
  TEST_ASSERT_TRUE(f.params().bounce < 0);  // moving up
  f.update(279, false);
  TEST_ASSERT_EQUAL_FLOAT(-14.0f, f.targets().bounce);
  f.update(280, false);  // 180 ms after the tap: hop comes down
  TEST_ASSERT_EQUAL_FLOAT(0.0f, f.targets().bounce);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);  // still happy
  f.update(1000, false);                            // tap + 900: held (strict)
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);
  f.update(1001, false);  // back to resting
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(0.55f, f.targets().curve);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().width);
}

static void test_double_tap_surprised() {
  Face f(5, 0);
  f.update(100, false);
  f.tap(100);
  f.tap(419);  // 319 ms later: surprised
  f.update(420, false);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(1.15f, f.targets().width);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().mouthO);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, f.targets().curve);
  TEST_ASSERT_EQUAL_FLOAT(-6.0f, f.targets().bounce);
  for (uint32_t t = 430; t <= 1710; t += 10) f.update(t, false);
  TEST_ASSERT_EQUAL_FLOAT(-6.0f, f.targets().bounce);  // the first tap's hop reset doesn't apply
  TEST_ASSERT_TRUE(f.geometry(1710).mouthIsO);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 22, f.geometry(1710).oRx);
  f.update(1719, false);  // 419 + 1300: held (strict)
  TEST_ASSERT_EQUAL_FLOAT(1.25f, f.targets().open);
  f.update(1720, false);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, f.targets().mouthO);
  for (uint32_t t = 1730; t < 3000; t += 10) f.update(t, false);
  TEST_ASSERT_FALSE(f.geometry(3000).mouthIsO);  // eased back to the curve
}

static void test_taps_320_ms_apart_are_two_happies() {
  Face f(5, 0);
  f.tap(100);
  f.tap(420);
  f.update(421, false);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(-14.0f, f.targets().bounce);
}

static void test_first_tap_is_never_double() {
  Face f(5, 0);
  f.tap(10);  // within 320 ms of t = 0, but there was no earlier tap
  f.update(11, false);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);
}

static void test_sleepy_when_dimmed() {
  Face f(5, 0);
  f.update(10, true);
  TEST_ASSERT_EQUAL_FLOAT(0.45f, f.targets().open);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, f.targets().curve);
  f.tap(20);  // a tap still reacts...
  f.update(30, true);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, f.targets().open);
  f.update(921, true);  // ...then back to sleepy, not resting
  TEST_ASSERT_EQUAL_FLOAT(0.45f, f.targets().open);
  f.update(931, false);  // undimmed: resting
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f.targets().open);
}

// ---- easing ----

static void test_easing_matches_mockup_per_frame_rate() {
  Face f(5, 0);
  f.tap(0);
  f.update(167, false);  // 10 mockup frames of 16.7 ms at k = 0.16
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.3f + 0.7f * powf(0.84f, 10), f.params().open);
}

static void test_easing_frame_rate_independent() {
  Face a(5, 0), b(5, 0);
  a.tap(10);
  b.tap(10);
  for (uint32_t t = 20; t <= 190; t += 10) a.update(t, false);  // 100 fps
  for (uint32_t t = 40; t <= 190; t += 30) b.update(t, false);  // ~33 fps
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a.params().open, b.params().open);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a.params().curve, b.params().curve);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a.params().width, b.params().width);
}

static void test_time_going_backwards_is_harmless() {
  Face f(5, 1000);
  f.update(1100, false);
  f.tap(1100);
  const float open = f.params().open;
  f.update(900, false);  // dt < 0 → no easing step
  TEST_ASSERT_EQUAL_FLOAT(open, f.params().open);
  TEST_ASSERT_FALSE(isnan(f.geometry(900).eyeL.h));
}

static void test_blinks_resume_after_long_gap() {
  Face f(7, 0);
  for (uint32_t t = 0; t <= 5000; t += 16) f.update(t, false);
  // More than 2^31 ms (24.8 days) without update(): a signed comparison
  // against the old schedule would read as "not yet" for another 24.8 days.
  const uint32_t t0 = 0x90000000u;
  int blinks = 0;
  bool inBlink = false;
  for (uint32_t t = t0; t - t0 < 15000; t += 2) {
    f.update(t, false);
    const bool dip = f.geometry(t).eyeL.h < 118 * f.params().open - 0.5f;
    if (dip && !inBlink) blinks++;
    inBlink = dip;
  }
  TEST_ASSERT_GREATER_OR_EQUAL(3, blinks);  // first one right away, then every 2-6 s
}

// ---- lively mode ----

static void test_lively_off_by_default_never_acts() {
  Face f(5, 0);
  for (uint32_t t = 0; t < 120000; t += 20) {
    f.update(t, false);
    TEST_ASSERT_EQUAL(0, int(f.act()));
  }
  TEST_ASSERT_FLOAT_WITHIN(EPS, 184, f.geometry(120000).mx - 8 * f.params().lookX);  // no wander
}

static void test_lively_acts_every_few_seconds_and_all_kinds_show_up() {
  Face f(11, 0);
  f.setLively(true, 0);
  bool seen[5] = {};
  int starts = 0, last = 0;
  uint32_t lastStart = 0, firstStart = 0;
  for (uint32_t t = 0; t < 600000; t += 20) {
    f.update(t, false);
    const int a = int(f.act());
    if (a && !last) {
      if (!starts) firstStart = t;
      if (starts) TEST_ASSERT_TRUE(t - lastStart >= 5000 && t - lastStart <= 11000 + 1800 + 40);
      lastStart = t;
      starts++;
    }
    seen[a] = true;
    last = a;
  }
  TEST_ASSERT_TRUE(firstStart > 5000 && firstStart < 5100);  // first one 5 s after switching on
  TEST_ASSERT_GREATER_THAN(40, starts);
  for (int k = 1; k <= 4; k++) TEST_ASSERT_TRUE(seen[k]);
}

static void test_lively_wander_stays_within_bounds() {
  Face f(3, 0);
  f.setLively(true, 0);
  float minX = 1e9f, maxX = -1e9f;
  for (uint32_t t = 0; t < 80000; t += 250) {
    const Geometry g = f.geometry(t);
    const float cx = g.eyeL.x + g.eyeL.w / 2;  // look is 0 without update()
    minX = fminf(minX, cx);
    maxX = fmaxf(maxX, cx);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 124 - 16, minX);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 124 + 16, maxX);
}

static void test_tap_cancels_an_act() {
  Face f(11, 0);
  f.setLively(true, 0);
  uint32_t t = 0;
  for (; t < 20000 && !int(f.act()); t += 20) f.update(t, false);
  TEST_ASSERT_TRUE(int(f.act()) != 0);
  f.tap(t);
  TEST_ASSERT_EQUAL(0, int(f.act()));
  TEST_ASSERT_EQUAL_FLOAT(1, f.targets().curve);  // the happy face, not the act's
}

// ---- moods, reactions, scaling (Hephy) ----

static void run(Face &f, uint32_t from, uint32_t to) {
  for (uint32_t t = from; t <= to; t += 33) f.update(t, false);
}

static void test_scaled_geometry() {
  const Geometry g = computeGeometry(REST, 1, 0);
  const Geometry s = scaled(g, 0.5f, 184, 257);  // about its own centre
  TEST_ASSERT_FLOAT_WITHIN(EPS, 184 + (g.eyeL.x - 184) * 0.5f, s.eyeL.x);
  TEST_ASSERT_FLOAT_WITHIN(EPS, g.eyeL.w * 0.5f, s.eyeL.w);
  TEST_ASSERT_FLOAT_WITHIN(EPS, g.eyeL.r * 0.5f, s.eyeL.r);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 257 + (318 - 257) * 0.5f, s.my);
  const Geometry m = scaled(g, 1, 100, 157);  // a plain move
  TEST_ASSERT_FLOAT_WITHIN(EPS, g.eyeR.x - 84, m.eyeR.x);
  TEST_ASSERT_FLOAT_WITHIN(EPS, g.eyeR.y - 100, m.eyeR.y);
}

static void test_moods_set_the_resting_face() {
  Face f(3, 0);
  f.setMood(Mood::Asleep);
  run(f, 0, 3000);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0, f.params().open);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0, f.params().lookX);
  TEST_ASSERT_EQUAL_FLOAT(8, computeGeometry(f.params(), 1, 0).eyeL.h);  // a closed pill
  f.setMood(Mood::Happy);
  run(f, 3000, 5000);
  TEST_ASSERT_TRUE(f.params().curve > 0.9f);
  f.setMood(Mood::Sad);
  run(f, 5000, 9000);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0, f.params().curve);
  TEST_ASSERT_TRUE(f.targets().lookY > 0.4f);  // looking down
  f.setMood(Mood::Focused);
  run(f, 9000, 12000);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.55f, f.params().open);
}

static void test_asleep_does_no_idle_acts() {
  Face f(9, 0);
  f.setLively(true, 0);
  f.setMood(Mood::Asleep);
  for (uint32_t t = 0; t < 60000; t += 33) {
    f.update(t, false);
    TEST_ASSERT_EQUAL(0, int(f.act()));
  }
}

static void test_chew_opens_and_closes_the_mouth() {
  Face f(1, 0);
  run(f, 0, 1000);
  f.react(Reaction::Chew, 1000);
  bool open = false, closed = false;
  for (uint32_t t = 1000; t < 2200; t += 20) {
    f.update(t, false);
    open |= computeGeometry(f.params(), 1, 0).mouthIsO;
    closed |= !computeGeometry(f.params(), 1, 0).mouthIsO;
  }
  TEST_ASSERT_TRUE(open && closed);
  run(f, 2200, 4000);
  TEST_ASSERT_FALSE(f.reacting());
  TEST_ASSERT_FALSE(computeGeometry(f.params(), 1, 0).mouthIsO);
}

static void test_shake_head_looks_both_ways() {
  Face f(1, 0);
  f.react(Reaction::ShakeHead, 0);
  float minX = 0, maxX = 0;
  for (uint32_t t = 0; t < 900; t += 20) {
    f.update(t, false);
    minX = fminf(minX, f.params().lookX);
    maxX = fmaxf(maxX, f.params().lookX);
  }
  TEST_ASSERT_TRUE(minX < -0.5f && maxX > 0.5f);
}

static void test_startle_wakes_wide_eyed() {
  Face f(1, 0);
  f.setMood(Mood::Asleep);
  run(f, 0, 2000);
  f.react(Reaction::Startle, 2000);
  f.setMood(Mood::Happy);
  run(f, 2000, 2400);
  TEST_ASSERT_TRUE(f.params().open > 1.0f);
  TEST_ASSERT_TRUE(computeGeometry(f.params(), 1, 0).mouthIsO);
  run(f, 2400, 4500);
  TEST_ASSERT_FALSE(f.reacting());
  TEST_ASSERT_TRUE(f.params().curve > 0.9f);  // happy afterwards
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rest_geometry_matches_mockup);
  RUN_TEST(test_face_at_start_is_rest_geometry);
  RUN_TEST(test_look_bounce_width_formulas);
  RUN_TEST(test_closed_eye_is_pill);
  RUN_TEST(test_o_mouth_threshold_and_size);
  RUN_TEST(test_breathing);
  RUN_TEST(test_first_blink_after_1500_ms);
  RUN_TEST(test_blinks_resume_after_long_gap);
  RUN_TEST(test_first_look_after_1200_ms_uses_seeded_draws);
  RUN_TEST(test_deterministic_for_seed);
  RUN_TEST(test_blink_intervals);
  RUN_TEST(test_blink_intervals_dimmed_are_slower);
  RUN_TEST(test_look_targets_range_and_timing);
  RUN_TEST(test_happy_tap_and_hop);
  RUN_TEST(test_double_tap_surprised);
  RUN_TEST(test_taps_320_ms_apart_are_two_happies);
  RUN_TEST(test_first_tap_is_never_double);
  RUN_TEST(test_sleepy_when_dimmed);
  RUN_TEST(test_easing_matches_mockup_per_frame_rate);
  RUN_TEST(test_easing_frame_rate_independent);
  RUN_TEST(test_time_going_backwards_is_harmless);
  RUN_TEST(test_lively_off_by_default_never_acts);
  RUN_TEST(test_lively_acts_every_few_seconds_and_all_kinds_show_up);
  RUN_TEST(test_lively_wander_stays_within_bounds);
  RUN_TEST(test_tap_cancels_an_act);
  RUN_TEST(test_scaled_geometry);
  RUN_TEST(test_moods_set_the_resting_face);
  RUN_TEST(test_asleep_does_no_idle_acts);
  RUN_TEST(test_chew_opens_and_closes_the_mouth);
  RUN_TEST(test_shake_head_looks_both_ways);
  RUN_TEST(test_startle_wakes_wide_eyed);
  return UNITY_END();
}
