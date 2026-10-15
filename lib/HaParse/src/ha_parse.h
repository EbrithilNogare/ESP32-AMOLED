// Home Assistant REST helpers for the lights app (docs/apps/home-assistant.md):
// parse light states from response bodies, build the toggle body, and map
// HTTP status codes. No networking here: HaClient does the I/O.
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ha {

enum class LightState : uint8_t { Unknown, On, Off, Unavailable };
enum class Result : uint8_t { Ok, Unauthorized, NotFound, NetworkError, ServerError, BadResponse, HostNotFound };

// Body of GET /api/states/<entity>: {"entity_id":…,"state":"on",…}.
// "on"/"off"/"unavailable" map to their states; anything else, a missing
// state or invalid JSON → Unknown. Attributes are skipped while parsing, so a
// big body costs no memory for them.
LightState parseState(const char *json, size_t len);

// Body of POST /api/services/light/toggle: an array of changed state objects.
// True and out set if entityId is in it; false if missing or invalid JSON.
bool findInStateArray(const char *json, size_t len, const char *entityId, LightState &out);

// Writes {"entity_id":"light.x"} with a terminating NUL. Returns the length
// (without the NUL), or 0 if it doesn't fit or the id would need escaping.
size_t buildToggleBody(char *buf, size_t cap, const char *entityId);

// 200/201 Ok, 401/403 Unauthorized, 404 NotFound, < 0 (HTTP client error
// codes) NetworkError, 5xx ServerError, anything else BadResponse.
Result mapHttpStatus(int code);

// ---- the light list (GET /api/states) ----

constexpr size_t kEntityMax = 48;  // with the NUL; longer ids are skipped (they couldn't be toggled)
constexpr size_t kNameMax = 32;    // with the NUL; longer names are cut

struct LightInfo {
  char entity[kEntityMax];
  char name[kNameMax];  // friendly_name, else the id without "light."
  LightState state;
};

// Case-insensitive name order, then entity id: the order tiles are shown in.
bool lightBefore(const LightInfo &a, const LightInfo &b);

// Inserts `l` into the sorted `list` of `n` (capacity `max`), dropping the
// last one when full. Returns the new count.
int insertLight(LightInfo *list, int n, int max, const LightInfo &l);

// Fills `l` from one state object; false unless it's a light.* entity whose
// id fits.
bool lightFromState(JsonObjectConst o, LightInfo &l);

// Collects the light.* entities from a GET /api/states body, an array of
// {entity_id, state, attributes:{friendly_name}, …}. It is read one element
// at a time from `in` (anything deserializeJson reads: the HTTP stream on the
// device, a buffer in tests), so memory stays small however many entities
// Home Assistant has. Keeps the first `max` by name, sorted. Returns the
// count, or -1 if the body isn't a JSON array of objects.
// A reader with one character of push-back (deserializeJson can't peek).
template <typename Reader>
struct PushbackReader {
  Reader &in;
  int back = -2;
  int read() {
    if (back == -2) return in.read();
    const int c = back;
    back = -2;
    return c;
  }
  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (int c; i < n && (c = read()) >= 0;) buf[i++] = char(c);
    return i;
  }
};

template <typename Reader>
int parseLightList(Reader &source, LightInfo *out, int max) {
  PushbackReader<Reader> in{source};
  auto next = [&in]() {  // next non-blank character, -1 at the end
    int c;
    do c = in.read();
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    return c;
  };
  if (next() != '[') return -1;
  const int first = next();
  if (first == ']') return 0;
  in.back = first;
  JsonDocument filter;
  filter["entity_id"] = true;
  filter["state"] = true;
  filter["attributes"]["friendly_name"] = true;
  int n = 0;
  for (;;) {
    JsonDocument doc;
    if (deserializeJson(doc, in, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(32)))
      return -1;
    if (!doc.is<JsonObjectConst>()) return -1;
    LightInfo l;
    if (lightFromState(doc.as<JsonObjectConst>(), l)) n = insertLight(out, n, max, l);
    const int c = next();
    if (c == ']') return n;
    if (c != ',') return -1;
  }
}
int parseLightList(const char *json, size_t len, LightInfo *out, int max);

const char *lightStateName(LightState s);
const char *resultName(Result r);

}  // namespace ha
