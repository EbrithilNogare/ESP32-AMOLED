#pragma once
#include <ha_parse.h>
#include <stdint.h>

// Home Assistant REST client (docs/apps/home-assistant.md). Requests run in
// their own FreeRTOS task so the UI never waits on the network; each finished
// request posts an Ev::HaResult event (a = light index,
// b = (toggle << 16) | (LightState << 8) | Result).
namespace apps::ha_client {

struct Light {
  const char *entity;
  const char *label;
};
constexpr int kLights = 4;
const Light &light(int i);

void begin();
bool refreshAll();   // GET each light's state; false if the queue is full
bool toggle(int i);  // POST light/toggle
bool busy();         // a request is queued or running

inline uint32_t pack(ha::LightState s, ha::Result r, bool toggle) {
  return (uint32_t(toggle) << 16) | (uint32_t(s) << 8) | uint32_t(r);
}
inline ha::LightState stateOf(uint32_t b) { return ha::LightState((b >> 8) & 0xFF); }
inline ha::Result resultOf(uint32_t b) { return ha::Result(b & 0xFF); }
inline bool isToggle(uint32_t b) { return (b >> 16) & 1; }

}  // namespace apps::ha_client
