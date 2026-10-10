#include <multi_tap.h>
#include <string.h>
#include <unity.h>

using namespace multitap;

void setUp() {}
void tearDown() {}

static void type(MultiTap &t, const char *keys, uint32_t &now, uint32_t gap) {
  for (const char *k = keys; *k; k++) {
    t.press(*k, now);
    now += gap;
  }
}

static void test_repeated_key_cycles() {
  MultiTap t(900);
  uint32_t now = 1000;
  type(t, "222", now, 200);  // a → b → c
  TEST_ASSERT_EQUAL_STRING("c", t.text());
  TEST_ASSERT_TRUE(t.pending());
  type(t, "22", now, 200);  // 2 → wraps to a
  TEST_ASSERT_EQUAL_STRING("a", t.text());
}

static void test_pause_commits() {
  MultiTap t(900);
  uint32_t now = 0;
  t.press('4', now);  // g
  TEST_ASSERT_FALSE(t.update(now + 899));
  TEST_ASSERT_TRUE(t.update(now + 900));
  TEST_ASSERT_FALSE(t.pending());
  t.press('4', now + 1000);  // a new character, not h
  TEST_ASSERT_EQUAL_STRING("gg", t.text());
}

static void test_other_key_commits() {
  MultiTap t;
  uint32_t now = 0;
  type(t, "4444", now, 100);  // g h i 4: the key's digit is last
  TEST_ASSERT_EQUAL_STRING("4", t.text());
  t.clear();
  now += 2000;
  type(t, "4433555", now, 100);  // h, e, l: a different key commits the last one
  TEST_ASSERT_EQUAL_STRING("hel", t.text());
}

static void test_modes() {
  MultiTap t;
  uint32_t now = 0;
  t.press('#', now);  // ABC
  TEST_ASSERT_EQUAL(Mode::Upper, t.mode());
  type(t, "2", now, 100);
  t.press('#', now);  // 123: also commits the pending "A"
  TEST_ASSERT_EQUAL(Mode::Digits, t.mode());
  type(t, "77", now, 100);  // digits commit at once: two 7s
  TEST_ASSERT_EQUAL_STRING("A77", t.text());
  TEST_ASSERT_FALSE(t.pending());
  t.press('#', now);
  TEST_ASSERT_EQUAL(Mode::Lower, t.mode());
  TEST_ASSERT_EQUAL_STRING("abc", modeName(Mode::Lower));
}

static void test_space_symbols_and_punctuation() {
  MultiTap t;
  uint32_t now = 0;
  t.press('0', now);  // space
  now += 2000;
  type(t, "11111", now, 100);  // . , - _ @
  now += 2000;
  type(t, "**", now, 100);  // # $
  TEST_ASSERT_EQUAL_STRING(" @$", t.text());
  TEST_ASSERT_EQUAL_STRING("", charsFor('#', Mode::Lower));
}

static void test_backspace() {
  MultiTap t;
  uint32_t now = 0;
  type(t, "2", now, 2000);
  type(t, "33", now, 100);  // a, e (pending)
  TEST_ASSERT_TRUE(t.backspace());
  TEST_ASSERT_EQUAL_STRING("a", t.text());
  TEST_ASSERT_FALSE(t.pending());
  t.press('2', now);  // a fresh character after the backspace, not a cycle
  TEST_ASSERT_EQUAL_STRING("aa", t.text());
  t.clear();
  TEST_ASSERT_FALSE(t.backspace());
}

static void test_max_length() {
  MultiTap t;
  uint32_t now = 0;
  t.press('#', now);
  t.press('#', now);  // digits: one character per press
  for (int i = 0; i < 70; i++) t.press('5', now++);
  TEST_ASSERT_EQUAL(kMaxLen, t.length());
  TEST_ASSERT_EQUAL(kMaxLen, strlen(t.text()));
}

static void test_cycling_at_max_length() {
  MultiTap t;
  uint32_t now = 0;
  for (size_t i = 0; i < kMaxLen; i++) {
    t.press('2', now);
    now += 1000;  // each one commits
  }
  t.press('3', now);  // full: ignored
  TEST_ASSERT_EQUAL(kMaxLen, t.length());
  TEST_ASSERT_EQUAL('a', t.text()[kMaxLen - 1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_repeated_key_cycles);
  RUN_TEST(test_pause_commits);
  RUN_TEST(test_other_key_commits);
  RUN_TEST(test_modes);
  RUN_TEST(test_space_symbols_and_punctuation);
  RUN_TEST(test_backspace);
  RUN_TEST(test_max_length);
  RUN_TEST(test_cycling_at_max_length);
  return UNITY_END();
}
