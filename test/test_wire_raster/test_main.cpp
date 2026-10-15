#include <string.h>
#include <unity.h>
#include <wire_raster.h>

using namespace raster;

void setUp() {}
void tearDown() {}

static uint16_t buf[32 * 24];
static Canvas canvas(bool swapped = false) {
  memset(buf, 0, sizeof buf);
  return Canvas{buf, 32, 24, swapped};
}
static uint16_t at(int x, int y) { return buf[y * 32 + x]; }

static void test_rgb565() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(0xFFFFFF));
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(0xFF0000));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0x00FF00));
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0x0000FF));
}

static void test_horizontal_line_covers_its_rows() {
  Canvas c = canvas();
  line(c, 2, 10, 12, 10, 2, 0xFFFFFF, 255);
  for (int x = 2; x < 12; x++) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, at(x, 9));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, at(x, 10));
    TEST_ASSERT_EQUAL_HEX16(0, at(x, 8));
    TEST_ASSERT_EQUAL_HEX16(0, at(x, 11));
  }
  TEST_ASSERT_EQUAL_HEX16(0, at(1, 10));  // square ends at the end points
  TEST_ASSERT_EQUAL_HEX16(0, at(12, 10));
}

static void test_half_coverage_blends_half() {
  Canvas c = canvas();
  line(c, 2, 10.5f, 12, 10.5f, 1, 0xFFFFFF, 255);  // [10, 11): exactly row 10
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, at(5, 10));
  Canvas d = canvas();
  line(d, 2, 10, 12, 10, 1, 0xFFFFFF, 255);  // [9.5, 10.5): half of row 9, half of row 10
  const uint16_t p = at(5, 9);
  TEST_ASSERT_TRUE(p != 0 && p != 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(p, at(5, 10));
}

static void test_steep_and_flat_are_symmetric() {
  Canvas c = canvas();
  line(c, 3, 2, 9, 20, 2, 0xFFFFFF, 255);
  uint16_t steep[32 * 24];
  memcpy(steep, buf, sizeof buf);
  // The same line mirrored on the diagonal must give the mirrored pixels.
  Canvas d = canvas();
  line(d, 2, 3, 20, 9, 2, 0xFFFFFF, 255);
  for (int y = 0; y < 24; y++)
    for (int x = 0; x < 24; x++) TEST_ASSERT_EQUAL_HEX16(steep[y * 32 + x], at(y, x));
}

static void test_direction_does_not_matter() {
  Canvas c = canvas();
  line(c, 4, 5, 25, 17, 2.5f, 0x66CCFF, 200);
  uint16_t forward[32 * 24];
  memcpy(forward, buf, sizeof buf);
  Canvas d = canvas();
  line(d, 25, 17, 4, 5, 2.5f, 0x66CCFF, 200);
  TEST_ASSERT_EQUAL_MEMORY(forward, buf, sizeof buf);
}

static void test_clipping_and_nothing_to_draw() {
  Canvas c = canvas();
  line(c, -50, -40, 80, 70, 3, 0xFFFFFF, 255);  // runs off both ends
  line(c, 5, 5, 5, 5, 2, 0xFFFFFF, 255);        // a point
  line(c, 1, 1, 30, 1, 2, 0xFFFFFF, 0);         // fully transparent
  line(c, 1, 1, 30, 1, 0, 0xFFFFFF, 255);       // no width
  TEST_ASSERT_EQUAL_HEX16(0, at(20, 1));
  TEST_ASSERT_NOT_EQUAL(0, at(10, 10));  // the diagonal crosses the canvas
}

static void test_swapped_pixels() {
  Canvas c = canvas(true);
  fill(c, 0, 0, 2, 1, rgb565(0xFF0000));
  TEST_ASSERT_EQUAL_HEX16(0x00F8, at(0, 0));
  line(c, 0, 6, 10, 6, 2, 0xFF0000, 255);
  TEST_ASSERT_EQUAL_HEX16(0x00F8, at(4, 5));
}

static void test_fill_clips() {
  Canvas c = canvas();
  fill(c, -5, -5, 3, 2, 0x1234);
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(2, 1));
  TEST_ASSERT_EQUAL_HEX16(0, at(3, 1));
  TEST_ASSERT_EQUAL_HEX16(0, at(0, 2));
  fill(c, 30, 20, 100, 100, 0x4321);
  TEST_ASSERT_EQUAL_HEX16(0x4321, at(31, 23));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rgb565);
  RUN_TEST(test_horizontal_line_covers_its_rows);
  RUN_TEST(test_half_coverage_blends_half);
  RUN_TEST(test_steep_and_flat_are_symmetric);
  RUN_TEST(test_direction_does_not_matter);
  RUN_TEST(test_clipping_and_nothing_to_draw);
  RUN_TEST(test_swapped_pixels);
  RUN_TEST(test_fill_clips);
  return UNITY_END();
}
