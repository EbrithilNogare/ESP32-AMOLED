#include <pet_core.h>
#include <unity.h>

using namespace pet;

void setUp() {}
void tearDown() {}

static const int64_t T0 = 1760000000;  // some unix time
static const int64_t H = 3600;

static void test_fresh_and_valid() {
  State s = fresh(T0, 7);
  TEST_ASSERT_TRUE(valid(s));
  TEST_ASSERT_EQUAL_UINT8(kNone, s.forging);
  TEST_ASSERT_EQUAL_UINT8(kNone, s.gift);
  TEST_ASSERT_EQUAL_INT(1, level(s.xp));
  TEST_ASSERT_EQUAL_INT(0, owned(s));
  State bad = s;
  bad.version = kVersion + 1;
  TEST_ASSERT_FALSE(valid(bad));
  bad = s;
  bad.energy = 150;
  TEST_ASSERT_FALSE(valid(bad));
  bad = s;
  bad.forging = kItems;
  TEST_ASSERT_FALSE(valid(bad));
}

static void test_items() {
  int n[4] = {};
  for (int i = 0; i < kItems; i++) n[int(item(i).rarity)]++;
  TEST_ASSERT_EQUAL_INT(10, n[0]);
  TEST_ASSERT_EQUAL_INT(7, n[1]);
  TEST_ASSERT_EQUAL_INT(5, n[2]);
  TEST_ASSERT_EQUAL_INT(2, n[3]);
  TEST_ASSERT_EQUAL_UINT32(12 * H, forgeSeconds(Rarity::Legendary));
}

static void test_meters_sink_to_a_floor_only() {
  State s = fresh(T0, 1);
  s.energy = 100;
  s.joy = 100;
  advance(s, T0 + 24 * H);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100 - 85.0f / 3, s.energy);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 60, s.joy);
  advance(s, T0 + 30 * 24 * H);  // a month away: floors, nothing worse
  TEST_ASSERT_EQUAL_FLOAT(15, s.energy);
  TEST_ASSERT_EQUAL_FLOAT(20, s.joy);
  s.energy = 5;  // already under the floor (spent): stays
  advance(s, T0 + 31 * 24 * H);
  TEST_ASSERT_EQUAL_FLOAT(5, s.energy);
}

static void test_clock_going_back_keeps_the_forge_time_left() {
  State s = fresh(T0, 1);
  TEST_ASSERT_NOT_EQUAL(kNone, startForge(s, T0));
  const int64_t left = forgeLeftSec(s, T0 + 600);
  const float energy = s.energy;
  advance(s, T0 + 600);
  advance(s, 946684800);                                               // the RTC lost power: back to 2000-01-01
  TEST_ASSERT_EQUAL_FLOAT(energy - 600 * 85.0f / (72 * H), s.energy);  // no decay from the jump
  TEST_ASSERT_EQUAL_INT64(left, forgeLeftSec(s, 946684800));
  advance(s, 946684800 + left);
  TEST_ASSERT_EQUAL_UINT8(kNone, s.forging);
  TEST_ASSERT_NOT_EQUAL(kNone, s.gift);
}

static void test_greetings_and_visit_bonus() {
  State s = fresh(T0, 1);
  TEST_ASSERT_EQUAL(Greeting::None, visit(s, T0 + 60));
  const float joy = s.joy;
  TEST_ASSERT_EQUAL(Greeting::Hello, visit(s, T0 + 60 + 2 * H));
  TEST_ASSERT_EQUAL_FLOAT(joy, s.joy);  // no bonus within 4 h of the last
  s.joy = 40;
  TEST_ASSERT_EQUAL(Greeting::MissedYou, visit(s, T0 + 60 + 2 * H + 13 * H));
  TEST_ASSERT_EQUAL_FLOAT(55, s.joy);
  TEST_ASSERT_EQUAL_UINT32(3, s.xp);
  TEST_ASSERT_EQUAL(Greeting::Asleep, visit(s, T0 + 60 + 2 * H + 13 * H + 25 * H));
  seen(s, T0 + 100 * H);
  TEST_ASSERT_EQUAL(Greeting::None, visit(s, T0 + 100 * H + 10));
}

static void test_moods() {
  State s = fresh(T0, 1);
  s.energy = 60;
  s.joy = 50;
  TEST_ASSERT_EQUAL(Mood::Content, mood(s));
  s.joy = 80;
  TEST_ASSERT_EQUAL(Mood::Happy, mood(s));
  s.joy = 30;
  TEST_ASSERT_EQUAL(Mood::Lonely, mood(s));
  s.energy = 20;
  TEST_ASSERT_EQUAL(Mood::Sleepy, mood(s));  // tired beats lonely
  s.forging = 0;
  TEST_ASSERT_EQUAL(Mood::Focused, mood(s));
}

static void test_feed() {
  State s = fresh(T0, 1);
  s.energy = 50;
  TEST_ASSERT_EQUAL(Result::Ok, feed(s));
  TEST_ASSERT_EQUAL_FLOAT(75, s.energy);
  TEST_ASSERT_EQUAL(Result::Ok, feed(s));
  TEST_ASSERT_EQUAL_FLOAT(100, s.energy);
  TEST_ASSERT_EQUAL(Result::Full, feed(s));
  TEST_ASSERT_EQUAL_UINT32(4, s.xp);
}

static void test_levels() {
  TEST_ASSERT_EQUAL_UINT32(0, levelStartXp(1));
  TEST_ASSERT_EQUAL_UINT32(25, levelStartXp(2));
  TEST_ASSERT_EQUAL_UINT32(75, levelStartXp(3));
  TEST_ASSERT_EQUAL_UINT32(150, levelStartXp(4));
  TEST_ASSERT_EQUAL_INT(1, level(24));
  TEST_ASSERT_EQUAL_INT(2, level(25));
  TEST_ASSERT_EQUAL_INT(3, level(149));
  TEST_ASSERT_EQUAL_INT(kMaxLevel, level(UINT32_MAX));
}

static void test_strike_scores_and_round() {
  TEST_ASSERT_EQUAL_UINT8(2, strikeScore(0));
  TEST_ASSERT_EQUAL_UINT8(2, strikeScore(-0.3f));
  TEST_ASSERT_EQUAL_UINT8(1, strikeScore(0.5f));
  TEST_ASSERT_EQUAL_UINT8(1, strikeScore(-1.0f));
  TEST_ASSERT_EQUAL_UINT8(0, strikeScore(1.2f));

  State s = fresh(T0, 1);
  s.energy = 60;
  s.joy = 40;
  const uint8_t scores[kStrikes] = {2, 2, 1, 0, 2};
  const Round r = finishRound(s, scores, T0);
  TEST_ASSERT_EQUAL_UINT8(3, r.perfect);
  TEST_ASSERT_EQUAL_UINT8(1, r.good);
  TEST_ASSERT_EQUAL_UINT32(7, r.xp);
  TEST_ASSERT_EQUAL_UINT32(0, r.forgeCutSec);  // nothing on the anvil
  TEST_ASSERT_EQUAL_FLOAT(40 + 12 + 2, s.joy);
  TEST_ASSERT_EQUAL_FLOAT(55, s.energy);
  s.energy = 9;
  TEST_ASSERT_EQUAL(Result::Tired, canStrike(s));
}

static void test_perfect_strikes_speed_up_the_forge() {
  State s = fresh(T0, 1);
  s.energy = 100;
  const uint8_t it = startForge(s, T0);
  TEST_ASSERT_EQUAL(Rarity::Common, item(it).rarity);  // level 1, seed 1: a common (75 %)
  const int64_t before = forgeLeftSec(s, T0);
  const uint8_t scores[kStrikes] = {2, 2, 2, 0, 0};
  const Round r = finishRound(s, scores, T0);
  TEST_ASSERT_EQUAL_UINT32(30 * 60, r.forgeCutSec);
  TEST_ASSERT_EQUAL_INT64(before - 30 * 60, forgeLeftSec(s, T0));
  // Near the end, the cut can't go past it: the forge finishes.
  const Round r2 = finishRound(s, scores, s.forgeEndSec - 60);
  TEST_ASSERT_EQUAL_UINT32(60, r2.forgeCutSec);
  TEST_ASSERT_EQUAL_UINT8(kNone, s.forging);
  TEST_ASSERT_EQUAL_UINT8(it, s.gift);
}

static void test_forge_rules() {
  State s = fresh(T0, 99);
  s.energy = 20;
  TEST_ASSERT_EQUAL(Result::Tired, canForge(s));
  TEST_ASSERT_EQUAL_UINT8(kNone, startForge(s, T0));
  s.energy = 60;
  const uint8_t it = startForge(s, T0);
  TEST_ASSERT_NOT_EQUAL(kNone, it);
  TEST_ASSERT_EQUAL_FLOAT(35, s.energy);
  TEST_ASSERT_EQUAL(Result::Busy, canForge(s));
  TEST_ASSERT_EQUAL_INT64(forgeSeconds(item(it).rarity), forgeLeftSec(s, T0));
  advance(s, T0 + forgeSeconds(item(it).rarity) - 1);
  TEST_ASSERT_EQUAL_UINT8(it, s.forging);
  advance(s, T0 + forgeSeconds(item(it).rarity));
  TEST_ASSERT_EQUAL_UINT8(kNone, s.forging);
  TEST_ASSERT_EQUAL_UINT8(it, s.gift);
  TEST_ASSERT_EQUAL(Result::Busy, canForge(s));  // collect first
  const Collected c = collect(s);
  TEST_ASSERT_EQUAL_UINT8(it, c.item);
  TEST_ASSERT_TRUE(c.isNew);
  TEST_ASSERT_EQUAL_UINT32(newItemXp(item(it).rarity), c.xp);
  TEST_ASSERT_TRUE(has(s, it));
  TEST_ASSERT_EQUAL_UINT16(1, s.forged);
  TEST_ASSERT_EQUAL_UINT8(kNone, collect(s).item);  // nothing more
}

static void test_forge_prefers_missing_items_then_duplicates() {
  State s = fresh(T0, 5);
  int64_t t = T0;
  // Common only at level 1 most of the time; forge until every common is
  // owned, checking that no common repeats while one is missing.
  int commons = 0, guard = 0;
  while (commons < 10 && guard++ < 200) {
    s.energy = 100;
    const uint8_t it = startForge(s, t);
    t = s.forgeEndSec;
    advance(s, t);
    const bool had = has(s, it);
    const Collected c = collect(s);
    if (item(it).rarity == Rarity::Common) {
      TEST_ASSERT_FALSE(had);
      TEST_ASSERT_TRUE(c.isNew);
      commons++;
    }
    s.xp = 0;  // stay level 1
  }
  TEST_ASSERT_EQUAL_INT(10, commons);
  // All commons owned: the next common is a duplicate worth half.
  for (guard = 0; guard < 200; guard++) {
    s.energy = 100;
    const uint8_t it = startForge(s, t);
    t = s.forgeEndSec;
    advance(s, t);
    const Collected c = collect(s);
    s.xp = 0;
    if (item(it).rarity == Rarity::Common) {
      TEST_ASSERT_FALSE(c.isNew);
      TEST_ASSERT_EQUAL_UINT32(5, c.xp);
      break;
    }
  }
  TEST_ASSERT_TRUE(guard < 200);
}

static void test_odds_follow_the_level() {
  int legendary[2] = {};
  for (int band = 0; band < 2; band++) {
    State s = fresh(T0, 12345);
    for (int i = 0; i < 2000; i++) {
      s.forging = s.gift = kNone;
      s.energy = 100;
      s.xp = band ? levelStartXp(6) : 0;
      legendary[band] += item(startForge(s, T0)).rarity == Rarity::Legendary;
    }
  }
  TEST_ASSERT_EQUAL_INT(0, legendary[0]);                    // never at level 1
  TEST_ASSERT_INT_WITHIN(40, 2000 * 6 / 100, legendary[1]);  // 6 % at level 6+
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_and_valid);
  RUN_TEST(test_items);
  RUN_TEST(test_meters_sink_to_a_floor_only);
  RUN_TEST(test_clock_going_back_keeps_the_forge_time_left);
  RUN_TEST(test_greetings_and_visit_bonus);
  RUN_TEST(test_moods);
  RUN_TEST(test_feed);
  RUN_TEST(test_levels);
  RUN_TEST(test_strike_scores_and_round);
  RUN_TEST(test_perfect_strikes_speed_up_the_forge);
  RUN_TEST(test_forge_rules);
  RUN_TEST(test_forge_prefers_missing_items_then_duplicates);
  RUN_TEST(test_odds_follow_the_level);
  return UNITY_END();
}
