#include "ha_parse.h"

#include <ArduinoJson.h>
#include <string.h>

namespace ha {

namespace {

// HA attributes can nest (e.g. effect lists, colour data); the default limit
// of 10 would reject such bodies even though the filter skips them.
constexpr uint8_t kNesting = 32;

LightState fromString(const char *s) {
  if (!s) return LightState::Unknown;
  if (strcmp(s, "on") == 0) return LightState::On;
  if (strcmp(s, "off") == 0) return LightState::Off;
  if (strcmp(s, "unavailable") == 0) return LightState::Unavailable;
  return LightState::Unknown;
}

}  // namespace

LightState parseState(const char *json, size_t len) {
  if (!json) return LightState::Unknown;
  // The filter keeps only the top-level "state", so the document stays tiny
  // whatever the attributes hold.
  JsonDocument filter;
  filter["state"] = true;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len, DeserializationOption::Filter(filter),
                                                   DeserializationOption::NestingLimit(kNesting));
  if (err) return LightState::Unknown;
  return fromString(doc["state"].as<const char *>());
}

bool findInStateArray(const char *json, size_t len, const char *entityId, LightState &out) {
  if (!json || !entityId) return false;
  // filter[0] applies to every array element.
  JsonDocument filter;
  filter[0]["entity_id"] = true;
  filter[0]["state"] = true;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len, DeserializationOption::Filter(filter),
                                                   DeserializationOption::NestingLimit(kNesting));
  if (err) return false;
  for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
    const char *id = item["entity_id"];
    if (id && strcmp(id, entityId) == 0) {
      out = fromString(item["state"].as<const char *>());
      return true;
    }
  }
  return false;
}

size_t buildToggleBody(char *buf, size_t cap, const char *entityId) {
  if (!buf || cap == 0) return 0;
  buf[0] = '\0';
  if (!entityId) return 0;
  // Entity ids are [a-z0-9_.]; refuse anything that would need JSON escaping
  // rather than send a malformed body.
  for (const char *c = entityId; *c; c++) {
    if (*c == '"' || *c == '\\' || static_cast<unsigned char>(*c) < 0x20) return 0;
  }
  static const char kHead[] = "{\"entity_id\":\"";
  static const char kTail[] = "\"}";
  const size_t head = sizeof(kHead) - 1, id = strlen(entityId), tail = sizeof(kTail) - 1;
  const size_t len = head + id + tail;
  if (len + 1 > cap) return 0;
  memcpy(buf, kHead, head);
  memcpy(buf + head, entityId, id);
  memcpy(buf + head + id, kTail, tail + 1);  // includes the NUL
  return len;
}

Result mapHttpStatus(int code) {
  if (code < 0) return Result::NetworkError;
  switch (code) {
    case 200:
    case 201: return Result::Ok;
    case 401:
    case 403: return Result::Unauthorized;
    case 404: return Result::NotFound;
    default: break;
  }
  if (code >= 500 && code <= 599) return Result::ServerError;
  return Result::BadResponse;
}

const char *lightStateName(LightState s) {
  switch (s) {
    case LightState::Unknown: return "unknown";
    case LightState::On: return "on";
    case LightState::Off: return "off";
    case LightState::Unavailable: return "unavailable";
  }
  return "?";
}

const char *resultName(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::Unauthorized: return "unauthorized";
    case Result::NotFound: return "not found";
    case Result::NetworkError: return "network error";
    case Result::ServerError: return "server error";
    case Result::BadResponse: return "bad response";
  }
  return "?";
}

}  // namespace ha
