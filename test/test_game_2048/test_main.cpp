#include <game_2048.h>
#include <unity.h>

using namespace g2048;

void setUp() {}
void tearDown() {}

// Rows from values (0 = empty), top to bottom.
static Board fromValues(const uint32_t v[kCells]) {
  Board b{};
  for (int i = 0; i < kCells; i++) {
    uint8_t e = 0;
    while (v[i] && (1u << e) < v[i]) e++;
    b.cell[i] = e;
  }
  return b;
}

static void assertValues(const uint32_t want[kCells], const Board &b) {
  for (int i = 0; i < kCells; i++) TEST_ASSERT_EQUAL_UINT32_MESSAGE(want[i], value(b.cell[i]), "cell");
}

static void test_value() {
  TEST_ASSERT_EQUAL_UINT32(0, value(0));
  TEST_ASSERT_EQUAL_UINT32(2, value(1));
  TEST_ASSERT_EQUAL_UINT32(2048, value(kWinExp));
}

static void test_left_merges_pairs_once() {
  const uint32_t in[kCells] = {2, 2, 2, 2, 2, 2, 4, 0, 4, 0, 4, 4, 8, 4, 4, 0};
  const uint32_t out[kCells] = {4, 4, 0, 0, 4, 4, 0, 0, 8, 4, 0, 0, 8, 8, 0, 0};
  Board b = fromValues(in);
  const Move m = slide(b, Dir::Left);
  assertValues(out, b);
  TEST_ASSERT_TRUE(m.moved);
  TEST_ASSERT_EQUAL_UINT32(4 + 4 + 4 + 8 + 8, m.gain);
  TEST_ASSERT_EQUAL_UINT32(m.gain, b.score);
  TEST_ASSERT_EQUAL_UINT8(5, m.nMerged);
}

static void test_right_up_down() {
  const uint32_t in[kCells] = {2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Board b = fromValues(in);
  slide(b, Dir::Right);
  const uint32_t right[kCells] = {0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  assertValues(right, b);
  slide(b, Dir::Down);
  const uint32_t down[kCells] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4};
  assertValues(down, b);
  slide(b, Dir::Up);
  const uint32_t up[kCells] = {0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  assertValues(up, b);
}

static void test_the_pair_nearest_the_wall_merges_first() {
  const uint32_t in[kCells] = {0, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Board b = fromValues(in);
  slide(b, Dir::Right);
  const uint32_t out[kCells] = {0, 0, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  assertValues(out, b);
}

static void test_nothing_moves() {
  const uint32_t in[kCells] = {2, 4, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Board b = fromValues(in);
  const Move m = slide(b, Dir::Left);
  TEST_ASSERT_FALSE(m.moved);
  TEST_ASSERT_EQUAL_UINT32(0, m.gain);
  assertValues(in, b);
  TEST_ASSERT_EQUAL_UINT8(3, m.nSlides);  // still reported, from == to
  for (int i = 0; i < m.nSlides; i++) TEST_ASSERT_EQUAL_UINT8(m.slides[i].from, m.slides[i].to);
}

static void test_slides_describe_the_move() {
  const uint32_t in[kCells] = {2, 0, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Board b = fromValues(in);
  const Move m = slide(b, Dir::Left);
  TEST_ASSERT_EQUAL_UINT8(3, m.nSlides);
  TEST_ASSERT_EQUAL_UINT8(0, m.slides[0].from);
  TEST_ASSERT_EQUAL_UINT8(0, m.slides[0].to);
  TEST_ASSERT_FALSE(m.slides[0].merged);
  TEST_ASSERT_EQUAL_UINT8(2, m.slides[1].from);  // the second 2 lands on the first
  TEST_ASSERT_EQUAL_UINT8(0, m.slides[1].to);
  TEST_ASSERT_TRUE(m.slides[1].merged);
  TEST_ASSERT_EQUAL_UINT8(3, m.slides[2].from);
  TEST_ASSERT_EQUAL_UINT8(1, m.slides[2].to);
  TEST_ASSERT_EQUAL_UINT8(1, m.nMerged);
  TEST_ASSERT_EQUAL_UINT8(0, m.mergedAt[0]);
}

static void test_win_once() {
  const uint32_t in[kCells] = {1024, 1024, 0, 0, 1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Board b = fromValues(in);
  Move m = slide(b, Dir::Left);
  TEST_ASSERT_TRUE(m.justWon);
  TEST_ASSERT_EQUAL_UINT8(1, b.won);
  TEST_ASSERT_EQUAL_UINT8(kWinExp, maxExp(b));
  m = slide(b, Dir::Up);  // 2048 + 2048: still won, not "just" won again
  TEST_ASSERT_FALSE(m.justWon);
  TEST_ASSERT_EQUAL_UINT8(kWinExp + 1, maxExp(b));
}

static void test_over() {
  const uint32_t full[kCells] = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};
  TEST_ASSERT_TRUE(over(fromValues(full)));
  const uint32_t pair[kCells] = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 4};
  TEST_ASSERT_FALSE(over(fromValues(pair)));  // the last two merge
  const uint32_t column[kCells] = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 2, 2, 4, 2};
  TEST_ASSERT_FALSE(over(fromValues(column)));
  const uint32_t hole[kCells] = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 0, 4, 4, 2, 4, 2};
  TEST_ASSERT_FALSE(over(fromValues(hole)));
}

static void test_new_game_and_spawn() {
  Rng rng{1234};
  Board b;
  b.score = 99;
  newGame(b, rng);
  TEST_ASSERT_EQUAL_INT(2, tiles(b));
  TEST_ASSERT_EQUAL_UINT32(0, b.score);
  TEST_ASSERT_EQUAL_UINT8(0, b.won);
  for (int i = 2; i < kCells; i++) TEST_ASSERT_NOT_EQUAL(-1, spawn(b, rng));
  TEST_ASSERT_EQUAL_INT(kCells, tiles(b));
  TEST_ASSERT_EQUAL_INT(-1, spawn(b, rng));
}

static void test_spawn_odds() {
  Rng rng{42};
  int fours = 0;
  const int n = 10000;
  for (int i = 0; i < n; i++) {
    Board b{};
    const int at = spawn(b, rng);
    TEST_ASSERT_TRUE(at >= 0 && at < kCells);
    TEST_ASSERT_TRUE(b.cell[at] == 1 || b.cell[at] == 2);
    fours += b.cell[at] == 2;
  }
  TEST_ASSERT_INT_WITHIN(150, n / 10, fours);
}

static void test_rng_below_is_bounded() {
  Rng rng{0};  // a zero seed still runs
  for (int i = 0; i < 1000; i++) TEST_ASSERT_TRUE(rng.below(7) < 7);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_value);
  RUN_TEST(test_left_merges_pairs_once);
  RUN_TEST(test_right_up_down);
  RUN_TEST(test_the_pair_nearest_the_wall_merges_first);
  RUN_TEST(test_nothing_moves);
  RUN_TEST(test_slides_describe_the_move);
  RUN_TEST(test_win_once);
  RUN_TEST(test_over);
  RUN_TEST(test_new_game_and_spawn);
  RUN_TEST(test_spawn_odds);
  RUN_TEST(test_rng_below_is_bounded);
  return UNITY_END();
}
