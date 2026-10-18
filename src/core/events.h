#pragma once
#include <stdint.h>

// One queue of small typed events between tasks and modules. Any task may
// post (not ISRs); only the UI task drains it, once per loop, so
// handlers can touch LVGL safely (ARCHITECTURE §6.3).
namespace core {

enum class Ev : uint8_t {
  Back,         // PWR short press
  Home,         // PWR held (BUTTON_LONG_PRESS_MS)
  Power,        // BOOT short press with the screen on: screen off, sleep
  PowerOff,     // BOOT held (BUTTON_POWER_OFF_MS): power off
  WifiChanged,  // a = 1 when connected; net::connected() already reflects it
  HaResult,     // a = light index, b = ha_client::pack(): state, result, toggle, list generation
  HaLights,     // the light list was loaded (or not): b = ha::Result
  TimeSynced,   // SNTP set the clock
};

struct Event {
  Ev type;
  uint32_t a;
  uint32_t b;
};

namespace events {
void begin();
bool post(const Event &e);
bool poll(Event &out);
}  // namespace events

}  // namespace core
