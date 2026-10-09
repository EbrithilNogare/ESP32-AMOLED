// Home Assistant REST helpers for the lights app (docs/apps/home-assistant.md):
// parse light states from response bodies, build the toggle body, and map
// HTTP status codes. No networking here: HaClient does the I/O.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ha {

enum class LightState : uint8_t { Unknown, On, Off, Unavailable };
enum class Result : uint8_t { Ok, Unauthorized, NotFound, NetworkError, ServerError, BadResponse };

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

const char *lightStateName(LightState s);
const char *resultName(Result r);

}  // namespace ha
