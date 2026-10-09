#include <dice_core.h>
#include <math.h>
#include <unity.h>
#include <initializer_list>

using namespace dice;

void setUp() {}
void tearDown() {}

// Deterministic sources for the roll tests.
static uint32_t seq[64];
static int seqPos;
static uint32_t fromSeq() { return seq[seqPos++ % 64]; }
static uint32_t lcgState = 12345;
static uint32_t lcg() { return lcgState = lcgState * 1664525u + 1013904223u; }

static void test_uniform_range_and_coverage() {
  for (uint8_t die : kDice) {
    bool seen[101] = {};
    for (int i = 0; i < 20000; i++) {
      const uint8_t v = uniform(lcg, die);
      TEST_ASSERT_TRUE(v >= 1 && v <= die);
      seen[v] = true;
    }
    for (int f = 1; f <= die; f++) TEST_ASSERT_TRUE_MESSAGE(seen[f], "every face comes up");
  }
}

static void test_uniform_rejects_biased_tail() {
  // The top values of the 32-bit range would bias % 6; they must be skipped.
  seqPos = 0;
  seq[0] = UINT32_MAX;  // in the rejected tail for n = 6
  seq[1] = 7;           // 7 % 6 + 1 = 2
  TEST_ASSERT_EQUAL_UINT8(2, uniform(fromSeq, 6));
}

static void test_advantage_keeps_higher_total() {
  seqPos = 0;
  seq[0] = 4, seq[1] = 16;  // d20: 5, then 17
  Roll r = roll(20, 1, Mode::Adv, fromSeq);
  TEST_ASSERT_EQUAL(2, r.sets);
  TEST_ASSERT_EQUAL(5, r.v[0][0]);
  TEST_ASSERT_EQUAL(17, r.v[1][0]);
  TEST_ASSERT_EQUAL(1, r.kept);

  seqPos = 0;
  r = roll(20, 1, Mode::Dis, fromSeq);
  TEST_ASSERT_EQUAL(0, r.kept);
}

static void test_tie_keeps_first_set() {
  seqPos = 0;
  seq[0] = 2, seq[1] = 2;
  TEST_ASSERT_EQUAL(0, roll(6, 1, Mode::Adv, fromSeq).kept);
  seqPos = 0;
  TEST_ASSERT_EQUAL(0, roll(6, 1, Mode::Dis, fromSeq).kept);
}

static void test_multi_dice_totals() {
  seqPos = 0;
  for (int i = 0; i < 12; i++) seq[i] = uint32_t(i);  // d6 faces 1..6, 1..6
  Roll r = roll(6, 6, Mode::Adv, fromSeq);
  TEST_ASSERT_EQUAL(21, r.total[0]);
  TEST_ASSERT_EQUAL(21, r.total[1]);
  TEST_ASSERT_EQUAL(Crit::None, r.crit);
}

static void test_crit_only_on_single_d20() {
  seqPos = 0;
  seq[0] = 19;  // 20
  TEST_ASSERT_EQUAL(Crit::Nat20, roll(20, 1, Mode::Flat, fromSeq).crit);
  seqPos = 0;
  seq[0] = 0;  // 1
  TEST_ASSERT_EQUAL(Crit::Nat1, roll(20, 1, Mode::Flat, fromSeq).crit);
  seqPos = 0;
  seq[0] = 19, seq[1] = 19;
  TEST_ASSERT_EQUAL(Crit::None, roll(20, 2, Mode::Flat, fromSeq).crit);
  // With advantage the crit comes from the kept die: 1 and 20 -> natural 20.
  seqPos = 0;
  seq[0] = 0, seq[1] = 19;
  TEST_ASSERT_EQUAL(Crit::Nat20, roll(20, 1, Mode::Adv, fromSeq).crit);
  seqPos = 0;
  TEST_ASSERT_EQUAL(Crit::Nat1, roll(20, 1, Mode::Dis, fromSeq).crit);
}

static void test_count_is_clamped() {
  TEST_ASSERT_EQUAL(kMaxCount, roll(6, 50, Mode::Flat, lcg).count);
  TEST_ASSERT_EQUAL(kMaxCountD100, roll(100, 9, Mode::Flat, lcg).count);
  TEST_ASSERT_EQUAL(1, roll(6, 0, Mode::Flat, lcg).count);
  TEST_ASSERT_EQUAL(20, roll(7, 1, Mode::Flat, lcg).die);  // unknown die -> d20
}

static void test_idle_shows_maximum() {
  const Roll r = idle(12, 3, Mode::Adv);
  TEST_ASSERT_EQUAL(36, r.total[0]);
  TEST_ASSERT_EQUAL(36, r.total[1]);
}

static void test_percentile_split() {
  Percentile p = split(100);
  TEST_ASSERT_EQUAL(0, p.tens);
  TEST_ASSERT_EQUAL(0, p.ones);
  p = split(7);
  TEST_ASSERT_EQUAL(0, p.tens);
  TEST_ASSERT_EQUAL(7, p.ones);
  p = split(40);
  TEST_ASSERT_EQUAL(40, p.tens);
  TEST_ASSERT_EQUAL(0, p.ones);
  p = split(99);
  TEST_ASSERT_EQUAL(90, p.tens);
  TEST_ASSERT_EQUAL(9, p.ones);
}

static void test_solid_counts() {
  const int nv[] = {4, 8, 6, 12, 20, 12}, ne[] = {6, 12, 12, 20, 30, 30}, nf[] = {3, 4, 3, 4, 5, 3};
  const uint8_t dies[] = {4, 6, 8, 10, 12, 20};
  for (int i = 0; i < 6; i++) {
    const Solid &s = solid(dies[i]);
    TEST_ASSERT_EQUAL_MESSAGE(nv[i], s.nv, "vertices");
    TEST_ASSERT_EQUAL_MESSAGE(ne[i], s.ne, "edges");
    TEST_ASSERT_EQUAL_MESSAGE(nf[i], s.nface, "face vertices");
  }
  TEST_ASSERT_EQUAL_PTR(&solid(10), &solid(100));
}

static void test_rest_pose_is_a_rotation() {
  for (uint8_t die : {4, 6, 8, 10, 12, 20}) {
    const Mat3 &m = solid(die).rest;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) {
        float d = 0;
        for (int k = 0; k < 3; k++) d += m.m[i][k] * m.m[j][k];
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, i == j ? 1.0f : 0.0f, d);
      }
    const float det = m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
                      m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
                      m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, det);  // no mirroring
  }
}

// Landing face-on: in the rest pose the chosen face is the nearest to the
// viewer, all its vertices sit at one depth (flat towards the camera) and its
// centre projects onto the die's centre.
static void test_rest_pose_faces_viewer() {
  for (uint8_t die : {4, 6, 8, 12, 20}) {
    const Solid &s = solid(die);
    float z0 = 0, cx = 0, cy = 0, minZ = 1e9f;
    for (int i = 0; i < s.nv; i++) minZ = fminf(minZ, mul(s.rest, s.v[i]).z);
    for (int k = 0; k < s.nface; k++) {
      const Vec3 p = mul(s.rest, s.v[s.face[k]]);
      if (k == 0) z0 = p.z;
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, z0, p.z);
      cx += p.x / s.nface;
      cy += p.y / s.nface;
    }
    TEST_ASSERT_TRUE(z0 < 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, minZ, z0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, cx);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, cy);
  }
}

static void test_rest_pose_is_upright() {
  // Triangles and the pentagon: a vertex straight up. Cube: a horizontal top edge.
  for (uint8_t die : {4, 8, 12, 20}) {
    const Solid &s = solid(die);
    int top = 0;
    for (int k = 1; k < s.nface; k++)
      if (mul(s.rest, s.v[s.face[k]]).y < mul(s.rest, s.v[s.face[top]]).y) top = k;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, mul(s.rest, s.v[s.face[top]]).x);
  }
  const Solid &c = solid(6);
  float ys[4];
  for (int k = 0; k < 4; k++) ys[k] = mul(c.rest, c.v[c.face[k]]).y;
  int high = 0;
  for (int k = 0; k < 4; k++)
    if (ys[k] < -0.1f) high++;
  TEST_ASSERT_EQUAL(2, high);
  // d10: the top apex is straight above the face.
  const Solid &d = solid(10);
  const Vec3 apex = mul(d.rest, d.v[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, apex.x);
  TEST_ASSERT_TRUE(apex.y < 0);
}

static void test_spin_lands_exactly_on_rest() {
  const Solid &s = solid(20);
  const Vec3 axis = randomAxis(0.3f, 0.7f);
  const Mat3 end = spinPose(s, axis, 20.0f, 1.0f);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) TEST_ASSERT_EQUAL_FLOAT(s.rest.m[i][j], end.m[i][j]);
  const Mat3 mid = spinPose(s, axis, 20.0f, 0.5f);
  float diff = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) diff += fabsf(mid.m[i][j] - s.rest.m[i][j]);
  TEST_ASSERT_TRUE(diff > 0.01f);
}

static void test_face_vertices_are_ordered() {
  // Drawing order goes around the face: consecutive vertices are edges.
  for (uint8_t die : {4, 6, 8, 12, 20}) {
    const Solid &s = solid(die);
    for (int k = 0; k < s.nface; k++) {
      const int a = s.face[k], b = s.face[(k + 1) % s.nface];
      bool edge = false;
      for (int e = 0; e < s.ne; e++)
        if ((s.e[e][0] == a && s.e[e][1] == b) || (s.e[e][0] == b && s.e[e][1] == a)) edge = true;
      TEST_ASSERT_TRUE(edge);
    }
  }
}

static void test_projection_centre() {
  const Solid &s = solid(6);
  Point p[kMaxVerts];
  project(s, s.rest, 100, 200, 50, p);
  float cx = 0, cy = 0;
  for (int i = 0; i < s.nv; i++) cx += p[i].x / s.nv, cy += p[i].y / s.nv;
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100, cx);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 200, cy);
}

static void test_icons() {
  Segment seg[kMaxIconSegments];
  TEST_ASSERT_EQUAL(4, icon(6, seg));
  TEST_ASSERT_EQUAL(6, icon(4, seg));
  TEST_ASSERT_EQUAL(18, icon(20, seg));
  TEST_ASSERT_EQUAL(22, icon(100, seg));  // two d10s
  for (uint8_t die : kDice) {
    const int n = icon(die, seg);
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(fabsf(seg[i].x1) <= 1.25f && fabsf(seg[i].y1) <= 1.25f);
      TEST_ASSERT_TRUE(fabsf(seg[i].x2) <= 1.25f && fabsf(seg[i].y2) <= 1.25f);
    }
  }
}

static void test_layout_fits_rect() {
  Slot s[kMaxPieces];
  for (int n = 1; n <= kMaxCount; n++) {
    const float r = layoutGrid(n, 10, 20, 300, 200, 80, 1, s);
    TEST_ASSERT_TRUE(r > 0 && r <= 80);
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(s[i].x - r >= 10 - 0.01f && s[i].x + r <= 310 + 0.01f);
      TEST_ASSERT_TRUE(s[i].y - r >= 20 - 0.01f && s[i].y + r <= 220 + 0.01f);
    }
  }
  layoutGrid(1, 0, 0, 100, 100, 30, 1, s);
  TEST_ASSERT_EQUAL_FLOAT(50, s[0].x);
  TEST_ASSERT_EQUAL_FLOAT(30, s[0].r);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uniform_range_and_coverage);
  RUN_TEST(test_uniform_rejects_biased_tail);
  RUN_TEST(test_advantage_keeps_higher_total);
  RUN_TEST(test_tie_keeps_first_set);
  RUN_TEST(test_multi_dice_totals);
  RUN_TEST(test_crit_only_on_single_d20);
  RUN_TEST(test_count_is_clamped);
  RUN_TEST(test_idle_shows_maximum);
  RUN_TEST(test_percentile_split);
  RUN_TEST(test_solid_counts);
  RUN_TEST(test_rest_pose_is_a_rotation);
  RUN_TEST(test_rest_pose_faces_viewer);
  RUN_TEST(test_rest_pose_is_upright);
  RUN_TEST(test_spin_lands_exactly_on_rest);
  RUN_TEST(test_face_vertices_are_ordered);
  RUN_TEST(test_projection_centre);
  RUN_TEST(test_icons);
  RUN_TEST(test_layout_fits_rect);
  return UNITY_END();
}
