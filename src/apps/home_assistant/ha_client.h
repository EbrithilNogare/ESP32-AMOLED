#pragma once
#include <ha_parse.h>
#include <stdint.h>

#include "config.h"

// Home Assistant REST client (docs/apps/home-assistant.md). Requests run in
// their own FreeRTOS task so the UI never waits on the network:
// - loadLights() reads every light.* entity (GET /api/states) and posts
//   Ev::HaLights; the UI then calls adopt() to make it the list.
// - refresh() / toggle() post one Ev::HaResult per light (a = index,
//   b = pack(): state, result, toggle, the list generation it was for).
// The list and everything else here belong to the UI task.
namespace apps::ha_client {

constexpr int kMaxLights = HA_MAX_LIGHTS;

void begin();
bool loadLights();   // false if the queue is full or a list is still on its way
bool refresh();      // GET each light's state; false if the queue is full
bool toggle(int i);  // POST light/toggle
bool busy();         // a request is queued or running

int count();
const ha::LightInfo &light(int i);
uint8_t generation();  // changes with every new list
// Answers Ev::HaLights (its result): with Ok the loaded list becomes the
// list. True if that changed it in anything but the states.
bool adopt(ha::Result r);
void setList(const ha::LightInfo *lights, int n);  // e.g. from the NVS cache

inline uint32_t pack(ha::LightState s, ha::Result r, bool toggle, uint8_t gen) {
  return (uint32_t(gen) << 24) | (uint32_t(toggle) << 16) | (uint32_t(s) << 8) | uint32_t(r);
}
inline ha::LightState stateOf(uint32_t b) { return ha::LightState((b >> 8) & 0xFF); }
inline ha::Result resultOf(uint32_t b) { return ha::Result(b & 0xFF); }
inline bool isToggle(uint32_t b) { return (b >> 16) & 1; }
inline uint8_t generationOf(uint32_t b) { return uint8_t(b >> 24); }

}  // namespace apps::ha_client
