#include <button_logic.h>
#include <unity.h>

using namespace btn;

static const Config C = {30, 1000};

void setUp() {}
void tearDown() {}

// Feeds a constant level from..to (inclusive) every 5 ms; returns the first
// non-None event (and where it happened), the rest must be None.
struct Hit {
  Event ev;
  uint32_t at;
};
static Hit feed(Button &b, bool level, uint32_t from, uint32_t to) {
  Hit hit = {Event::None, 0};
  for (uint32_t t = from; t <= to; t += 5) {
    const Event e = b.update(level, t);
    if (e != Event::None) {
      TEST_ASSERT_EQUAL_MESSAGE(Event::None, hit.ev, "more than one event");
      hit = {e, t};
    }
  }
  return hit;
}

static void test_idle_no_events() {
  Button b(C);
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 0, 5000).ev);
  TEST_ASSERT_FALSE(b.held());
}

static void test_pressed_after_exact_debounce() {
  Button b(C);
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 0));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 100));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 129));
  TEST_ASSERT_FALSE(b.held());
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, 130));
  TEST_ASSERT_TRUE(b.held());
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 131));  // only once
}

static void test_short_press() {
  Button b(C);
  b.update(false, 0);
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 100, 300).ev);
  const Hit h = feed(b, false, 305, 500);
  TEST_ASSERT_EQUAL(Event::Short, h.ev);
  TEST_ASSERT_EQUAL_UINT32(335, h.at);  // released at 305 + 30 ms debounce
  TEST_ASSERT_FALSE(b.held());
}

static void test_long_press_exact_threshold_no_short() {
  Button b(C);
  b.update(false, 0);
  b.update(true, 100);
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, 130));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 1129));
  TEST_ASSERT_EQUAL(Event::Long, b.update(true, 1130));  // 1000 ms after the debounced press
  TEST_ASSERT_EQUAL(Event::None, feed(b, true, 1135, 5000).ev);  // once
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 5005, 5200).ev);  // no Short after Long
  TEST_ASSERT_FALSE(b.held());
}

static void test_glitches_are_ignored() {
  Button b(C);
  b.update(false, 0);
  // 20 ms spikes, shorter than the debounce: nothing happens.
  for (uint32_t t = 100; t < 1000; t += 100) {
    TEST_ASSERT_EQUAL(Event::None, b.update(true, t));
    TEST_ASSERT_EQUAL(Event::None, b.update(true, t + 20));
    TEST_ASSERT_EQUAL(Event::None, b.update(false, t + 25));
    TEST_ASSERT_EQUAL(Event::None, b.update(false, t + 60));
  }
  TEST_ASSERT_FALSE(b.held());
}

static void test_bouncy_press_and_release_give_one_short() {
  Button b(C);
  b.update(false, 0);
  // Contact bounce at press: the debounce restarts on each change.
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 100));
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 105));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 110));
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 112));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 115));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 144));
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, 145));
  // Bounce while held doesn't release it.
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 300));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 310));
  TEST_ASSERT_TRUE(b.held());
  // Bouncy release: exactly one Short.
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 400));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 405));
  TEST_ASSERT_EQUAL(Event::None, b.update(false, 410));
  TEST_ASSERT_EQUAL(Event::Short, feed(b, false, 415, 600).ev);
}

static void test_suppress_cancels_short() {
  Button b(C);
  b.update(false, 0);
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 100, 200).ev);
  b.suppress();  // the press only woke the screen
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 205, 400).ev);
  // The next press is normal again.
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 500, 600).ev);
  TEST_ASSERT_EQUAL(Event::Short, feed(b, false, 605, 800).ev);
}

static void test_suppress_cancels_long() {
  Button b(C);
  b.update(false, 0);
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 100, 200).ev);
  b.suppress();
  TEST_ASSERT_EQUAL(Event::None, feed(b, true, 205, 3000).ev);
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 3005, 3200).ev);
}

static void test_suppress_during_debounce() {
  Button b(C);
  b.update(false, 0);
  b.update(true, 100);  // raw down, still debouncing
  b.suppress();
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 105, 2000).ev);  // Pressed still reported, no Long
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 2005, 2200).ev);
}

static void test_suppress_when_idle_or_glitch_does_not_leak() {
  Button b(C);
  b.update(false, 0);
  b.suppress();  // not pressed: no effect
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 100, 200).ev);
  TEST_ASSERT_EQUAL(Event::Short, feed(b, false, 205, 400).ev);
  // Suppressed glitch that never debounces: the next press is normal.
  b.update(true, 500);
  b.suppress();
  b.update(false, 510);
  TEST_ASSERT_EQUAL(Event::None, feed(b, false, 515, 700).ev);
  TEST_ASSERT_EQUAL(Event::Pressed, feed(b, true, 800, 900).ev);
  TEST_ASSERT_EQUAL(Event::Short, feed(b, false, 905, 1100).ev);
}

static void test_held_at_start() {
  // Button already down at boot (e.g. it woke the device): reported as a press.
  Button b(C);
  TEST_ASSERT_EQUAL(Event::None, b.update(true, 0));
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, 30));
}

static void test_millis_wraparound() {
  Button b(C);
  const uint32_t t0 = UINT32_MAX - 15;
  b.update(false, t0 - 100);
  TEST_ASSERT_EQUAL(Event::None, b.update(true, t0));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, t0 + 29));  // wraps past 0
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, t0 + 30));
  TEST_ASSERT_EQUAL(Event::None, b.update(true, t0 + 1029));
  TEST_ASSERT_EQUAL(Event::Long, b.update(true, t0 + 1030));
}

static void test_zero_debounce() {
  Button b({0, 500});
  TEST_ASSERT_EQUAL(Event::Pressed, b.update(true, 10));
  TEST_ASSERT_EQUAL(Event::Short, b.update(false, 20));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_no_events);
  RUN_TEST(test_pressed_after_exact_debounce);
  RUN_TEST(test_short_press);
  RUN_TEST(test_long_press_exact_threshold_no_short);
  RUN_TEST(test_glitches_are_ignored);
  RUN_TEST(test_bouncy_press_and_release_give_one_short);
  RUN_TEST(test_suppress_cancels_short);
  RUN_TEST(test_suppress_cancels_long);
  RUN_TEST(test_suppress_during_debounce);
  RUN_TEST(test_suppress_when_idle_or_glitch_does_not_leak);
  RUN_TEST(test_held_at_start);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_zero_debounce);
  return UNITY_END();
}
