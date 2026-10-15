#include <level_core.h>
#include <math.h>
#include <unity.h>

using namespace level;

void setUp() {}
void tearDown() {}

static void test_flat_face_up_is_level() {
  const Flat f = flatAngles({0, 0, 1});
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, f.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, f.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, tiltFromFlat({0, 0, 1}));
}

static void test_flat_signs() {
  // Right side lowered: the x axis points slightly down, so up has a negative x.
  TEST_ASSERT_TRUE(flatAngles({-0.05f, 0, 1}).x > 0);
  // Bottom (front) side lowered: the y axis points slightly up.
  TEST_ASSERT_TRUE(flatAngles({0, 0.05f, 1}).y > 0);
}

static void test_flat_round_trip() {
  for (float x = -20; x <= 20; x += 5)
    for (float y = -20; y <= 20; y += 5) {
      const Flat f = flatAngles(fromFlat(x, y));
      TEST_ASSERT_FLOAT_WITHIN(1e-3f, x, f.x);
      TEST_ASSERT_FLOAT_WITHIN(1e-3f, y, f.y);
    }
}

static void test_flat_works_face_down() {
  const Accel a = fromFlat(3, -2);
  const Flat f = flatAngles({a.x, a.y, -a.z});
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 3, f.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -2, f.y);
}

static void test_edge_round_trip() {
  for (float r = -170; r <= 170; r += 17)
    for (float l = -40; l <= 40; l += 10) {
      const Edge e = edgeAngles(fromEdge(r, l));
      TEST_ASSERT_FLOAT_WITHIN(1e-3f, r, e.rotation);
      TEST_ASSERT_FLOAT_WITHIN(1e-3f, l, e.lean);
    }
}

static void test_edge_upright_and_signs() {
  const Edge up = edgeAngles({0, 1, 0});
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, up.rotation);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, up.lean);
  // Turned clockwise: the right edge drops, so up leans toward -x.
  TEST_ASSERT_TRUE(edgeAngles({-0.1f, 1, 0}).rotation > 0);
  // Screen tipped toward the sky.
  TEST_ASSERT_TRUE(edgeAngles({0, 1, 0.1f}).lean > 0);
}

static void test_view_hysteresis() {
  TEST_ASSERT_EQUAL(View::Flat, chooseView(View::Flat, fromEdge(0, 45)));  // 45° from flat: stays
  TEST_ASSERT_EQUAL(View::Edge, chooseView(View::Edge, fromEdge(0, 45)));
  TEST_ASSERT_EQUAL(View::Edge, chooseView(View::Flat, fromEdge(0, 35)));  // 55° from flat
  TEST_ASSERT_EQUAL(View::Flat, chooseView(View::Edge, fromEdge(0, 55)));  // 35° from flat
  TEST_ASSERT_EQUAL(View::Flat, chooseView(View::Edge, {0.01f, 0, -1}));   // face down
}

static void test_nearest_ref() {
  Ref r = nearestRef(12);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, r.ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12, r.dev);
  TEST_ASSERT_FALSE(r.plumb);
  r = nearestRef(-88.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -90, r.ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, r.dev);
  TEST_ASSERT_TRUE(r.plumb);
  r = nearestRef(178);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 180, r.ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2, r.dev);
  r = nearestRef(-179);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 180, r.ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, r.dev);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -170, wrap180(190));
}

static void test_nearest_ref_hysteresis() {
  Ref r = nearestRef(44);
  TEST_ASSERT_FALSE(r.plumb);
  r = nearestRef(45.5f, &r);  // just past 45°: stays on level
  TEST_ASSERT_FALSE(r.plumb);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 45.5f, r.dev);
  r = nearestRef(47.5f, &r);  // closer to plumb by more than the hysteresis
  TEST_ASSERT_TRUE(r.plumb);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -42.5f, r.dev);
  r = nearestRef(45, &r);  // and back to 45°: stays on plumb
  TEST_ASSERT_TRUE(r.plumb);
  Ref top = nearestRef(-136);  // across ±180: dev stays small and signed
  top = nearestRef(-134.5f, &top);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 180, top.ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 45.5f, top.dev);
}

static void test_state_thresholds() {
  TEST_ASSERT_EQUAL(State::Level, stateOf(0.3f));
  TEST_ASSERT_EQUAL(State::Near, stateOf(-0.31f));
  TEST_ASSERT_EQUAL(State::Near, stateOf(2.0f));
  TEST_ASSERT_EQUAL(State::Far, stateOf(2.01f));
}

static void test_percent_and_bubble() {
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 100, percentSlope(45));
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, -100, percentSlope(-45));
  TEST_ASSERT_EQUAL_FLOAT(9999, percentSlope(89.999f));
  TEST_ASSERT_EQUAL_FLOAT(0, bubbleRadius(0, 90));
  TEST_ASSERT_TRUE(bubbleRadius(1, 90) < bubbleRadius(3, 90));
  TEST_ASSERT_TRUE(bubbleRadius(60, 90) <= 90);
  TEST_ASSERT_EQUAL_FLOAT(bubbleRadius(-4, 90), bubbleRadius(4, 90));
}

static void test_smoother() {
  Smoother s(0.5f);
  s.push({0, 0, 1});
  TEST_ASSERT_EQUAL_FLOAT(1, s.value().z);  // first sample passes through
  s.push({1, 0, 1});
  TEST_ASSERT_EQUAL_FLOAT(0.5f, s.value().x);
  s.reset();
  s.push({0, 1, 0});
  TEST_ASSERT_EQUAL_FLOAT(1, s.value().y);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flat_face_up_is_level);
  RUN_TEST(test_flat_signs);
  RUN_TEST(test_flat_round_trip);
  RUN_TEST(test_flat_works_face_down);
  RUN_TEST(test_edge_round_trip);
  RUN_TEST(test_edge_upright_and_signs);
  RUN_TEST(test_view_hysteresis);
  RUN_TEST(test_nearest_ref);
  RUN_TEST(test_nearest_ref_hysteresis);
  RUN_TEST(test_state_thresholds);
  RUN_TEST(test_percent_and_bubble);
  RUN_TEST(test_smoother);
  return UNITY_END();
}
