#include <ha_parse.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

using namespace ha;

void setUp() {}
void tearDown() {}

static LightState parse(const char *s) { return parseState(s, strlen(s)); }

static bool find(const char *s, const char *id, LightState &out) {
  return findInStateArray(s, strlen(s), id, out);
}

// ---- parseState ----

static void test_parse_basic_states() {
  TEST_ASSERT_EQUAL(LightState::On, parse(R"({"entity_id":"light.kitchen","state":"on"})"));
  TEST_ASSERT_EQUAL(LightState::Off, parse(R"({"entity_id":"light.kitchen","state":"off"})"));
  TEST_ASSERT_EQUAL(LightState::Unavailable, parse(R"({"state":"unavailable"})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":"unknown"})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":"ON"})"));  // HA is lowercase
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":""})"));
}

static void test_parse_real_ha_response() {
  const char *body =
      R"({"entity_id":"light.living_room","state":"on","attributes":{"min_color_temp_kelvin":2000,)"
      R"("max_color_temp_kelvin":6535,"supported_color_modes":["color_temp","xy"],"color_mode":"xy",)"
      R"("brightness":180,"hs_color":[29.79,65.88],"rgb_color":[255,167,86],"xy_color":[0.526,0.387],)"
      R"("effect_list":["blink","breathe","okay"],"friendly_name":"Living room","supported_features":44},)"
      R"("last_changed":"2026-10-09T10:00:00.000000+00:00","last_reported":"2026-10-09T10:00:00.000000+00:00",)"
      R"("last_updated":"2026-10-09T10:00:00.000000+00:00","context":{"id":"01J","parent_id":null,"user_id":null}})";
  TEST_ASSERT_EQUAL(LightState::On, parse(body));
}

static void test_parse_ignores_nested_state_keys() {
  // Only the top-level "state" counts, wherever it is in the object.
  TEST_ASSERT_EQUAL(LightState::Off,
                    parse(R"({"attributes":{"state":"on","x":{"state":"on"}},"state":"off"})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"attributes":{"state":"on"}})"));
}

static void test_parse_missing_or_wrong_type() {
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"entity_id":"light.kitchen"})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":1})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":null})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":["on"]})"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"(["on"])"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"("on")"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse("{}"));
}

static void test_parse_invalid_json() {
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(""));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse("not json"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":"on")"));  // truncated
  TEST_ASSERT_EQUAL(LightState::Unknown, parse(R"({"state":"on",)"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parse("<html>502 Bad Gateway</html>"));
  TEST_ASSERT_EQUAL(LightState::Unknown, parseState(nullptr, 10));
}

static void test_parse_respects_length() {
  // Only the first len bytes count: a body cut short is incomplete.
  const char *s = R"({"state":"on"}garbage)";
  TEST_ASSERT_EQUAL(LightState::On, parseState(s, 14));
  TEST_ASSERT_EQUAL(LightState::Unknown, parseState(s, 10));
}

static void test_parse_huge_attributes() {
  // ~60 KB of attributes, nested deeper than ArduinoJson's default limit of 10:
  // the filter skips them.
  static char body[64 * 1024];
  size_t n = 0;
  n += snprintf(body + n, sizeof(body) - n, R"({"entity_id":"light.big","attributes":{"deep":)");
  for (int i = 0; i < 10; i++) n += snprintf(body + n, sizeof(body) - n, "{\"a\":[");
  n += snprintf(body + n, sizeof(body) - n, "1");
  for (int i = 0; i < 10; i++) n += snprintf(body + n, sizeof(body) - n, "]}");
  n += snprintf(body + n, sizeof(body) - n, R"(,"list":[)");
  for (int i = 0; i < 2500 && n < sizeof(body) - 200; i++) {
    n += snprintf(body + n, sizeof(body) - n, "%s\"effect_%04d_xyz\"", i ? "," : "", i);
  }
  n += snprintf(body + n, sizeof(body) - n, R"(]},"state":"off"})");
  TEST_ASSERT_TRUE(n > 40000 && n < sizeof(body));
  TEST_ASSERT_EQUAL(LightState::Off, parseState(body, n));
}

// ---- findInStateArray ----

static const char *TOGGLE_RESPONSE =
    R"([{"entity_id":"light.kitchen","state":"on","attributes":{"brightness":255,"friendly_name":"Kitchen"},)"
    R"("last_changed":"2026-10-09T10:00:00+00:00","context":{"id":"x"}},)"
    R"({"entity_id":"light.hallway","state":"unavailable","attributes":{}},)"
    R"({"entity_id":"light.bedroom","state":"off"}])";

static void test_find_in_array() {
  LightState s = LightState::Unknown;
  TEST_ASSERT_TRUE(find(TOGGLE_RESPONSE, "light.kitchen", s));
  TEST_ASSERT_EQUAL(LightState::On, s);
  TEST_ASSERT_TRUE(find(TOGGLE_RESPONSE, "light.hallway", s));
  TEST_ASSERT_EQUAL(LightState::Unavailable, s);
  TEST_ASSERT_TRUE(find(TOGGLE_RESPONSE, "light.bedroom", s));
  TEST_ASSERT_EQUAL(LightState::Off, s);
}

static void test_find_missing_entity_leaves_out_untouched() {
  LightState s = LightState::On;
  TEST_ASSERT_FALSE(find(TOGGLE_RESPONSE, "light.living_room", s));
  TEST_ASSERT_FALSE(find(TOGGLE_RESPONSE, "light.kitch", s));  // no prefix match
  TEST_ASSERT_FALSE(find("[]", "light.kitchen", s));
  TEST_ASSERT_EQUAL(LightState::On, s);
}

static void test_find_entity_without_state() {
  LightState s = LightState::On;
  TEST_ASSERT_TRUE(find(R"([{"entity_id":"light.kitchen"}])", "light.kitchen", s));
  TEST_ASSERT_EQUAL(LightState::Unknown, s);
}

static void test_find_bad_input() {
  LightState s = LightState::On;
  TEST_ASSERT_FALSE(find(R"({"entity_id":"light.kitchen","state":"on"})", "light.kitchen", s));  // not an array
  TEST_ASSERT_FALSE(find(R"([{"entity_id":"light.kitchen","state":"on"})", "light.kitchen", s));  // truncated
  TEST_ASSERT_FALSE(find("garbage", "light.kitchen", s));
  TEST_ASSERT_FALSE(find(R"([1,"x",null,{"state":"on"}])", "light.kitchen", s));
  TEST_ASSERT_FALSE(findInStateArray(nullptr, 5, "light.kitchen", s));
  TEST_ASSERT_FALSE(find(TOGGLE_RESPONSE, nullptr, s));
  TEST_ASSERT_EQUAL(LightState::On, s);
}

// ---- buildToggleBody ----

static void test_build_toggle_body() {
  char buf[64];
  const size_t n = buildToggleBody(buf, sizeof(buf), "light.living_room");
  TEST_ASSERT_EQUAL_STRING(R"({"entity_id":"light.living_room"})", buf);
  TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

static void test_build_toggle_body_exact_fit() {
  const char *want = R"({"entity_id":"light.x"})";
  const size_t len = strlen(want);
  char buf[64];
  memset(buf, 'Z', sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(len, buildToggleBody(buf, len + 1, "light.x"));  // room for the NUL
  TEST_ASSERT_EQUAL_STRING(want, buf);
  memset(buf, 'Z', sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, len, "light.x"));  // one short
  TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
  TEST_ASSERT_EQUAL_CHAR('Z', buf[1]);  // nothing written past the empty string
}

static void test_build_toggle_body_rejects_bad_input() {
  char buf[64];
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, sizeof(buf), "light.\"x"));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, sizeof(buf), "light.\\x"));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, sizeof(buf), "light.\nx"));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, sizeof(buf), nullptr));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(nullptr, 64, "light.x"));
  TEST_ASSERT_EQUAL_size_t(0, buildToggleBody(buf, 0, "light.x"));
}

// ---- mapHttpStatus / names ----

static void test_map_http_status() {
  TEST_ASSERT_EQUAL(Result::Ok, mapHttpStatus(200));
  TEST_ASSERT_EQUAL(Result::Ok, mapHttpStatus(201));
  TEST_ASSERT_EQUAL(Result::Unauthorized, mapHttpStatus(401));
  TEST_ASSERT_EQUAL(Result::Unauthorized, mapHttpStatus(403));
  TEST_ASSERT_EQUAL(Result::NotFound, mapHttpStatus(404));
  TEST_ASSERT_EQUAL(Result::NetworkError, mapHttpStatus(-1));   // HTTPC_ERROR_CONNECTION_REFUSED
  TEST_ASSERT_EQUAL(Result::NetworkError, mapHttpStatus(-11));  // HTTPC_ERROR_READ_TIMEOUT
  TEST_ASSERT_EQUAL(Result::ServerError, mapHttpStatus(500));
  TEST_ASSERT_EQUAL(Result::ServerError, mapHttpStatus(502));
  TEST_ASSERT_EQUAL(Result::ServerError, mapHttpStatus(599));
  TEST_ASSERT_EQUAL(Result::BadResponse, mapHttpStatus(0));
  TEST_ASSERT_EQUAL(Result::BadResponse, mapHttpStatus(204));
  TEST_ASSERT_EQUAL(Result::BadResponse, mapHttpStatus(301));
  TEST_ASSERT_EQUAL(Result::BadResponse, mapHttpStatus(400));
  TEST_ASSERT_EQUAL(Result::BadResponse, mapHttpStatus(600));
}

static void test_names() {
  TEST_ASSERT_EQUAL_STRING("on", lightStateName(LightState::On));
  TEST_ASSERT_EQUAL_STRING("off", lightStateName(LightState::Off));
  TEST_ASSERT_EQUAL_STRING("unavailable", lightStateName(LightState::Unavailable));
  TEST_ASSERT_EQUAL_STRING("unknown", lightStateName(LightState::Unknown));
  TEST_ASSERT_EQUAL_STRING("unauthorized", resultName(Result::Unauthorized));
  TEST_ASSERT_EQUAL_STRING("ok", resultName(Result::Ok));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_basic_states);
  RUN_TEST(test_parse_real_ha_response);
  RUN_TEST(test_parse_ignores_nested_state_keys);
  RUN_TEST(test_parse_missing_or_wrong_type);
  RUN_TEST(test_parse_invalid_json);
  RUN_TEST(test_parse_respects_length);
  RUN_TEST(test_parse_huge_attributes);
  RUN_TEST(test_find_in_array);
  RUN_TEST(test_find_missing_entity_leaves_out_untouched);
  RUN_TEST(test_find_entity_without_state);
  RUN_TEST(test_find_bad_input);
  RUN_TEST(test_build_toggle_body);
  RUN_TEST(test_build_toggle_body_exact_fit);
  RUN_TEST(test_build_toggle_body_rejects_bad_input);
  RUN_TEST(test_map_http_status);
  RUN_TEST(test_names);
  return UNITY_END();
}
