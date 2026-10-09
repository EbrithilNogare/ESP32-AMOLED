#pragma once
#include <stdint.h>

// One queue of small typed events between tasks and modules. Anything may
// post (ISR-safe variant too); only the UI task drains it, once per loop, so
// handlers can touch LVGL safely (ARCHITECTURE §6.3).
namespace core {

enum class Ev : uint8_t {
  Back,         // Back button short press
  Home,         // Home button short press
  LongPress,    // either button held: power-off request
  VbusChanged,  // charger plugged/unplugged (a = 1 when present)
  WifiChanged,  // a = 1 when connected
  HaResult,     // a = light index, b = (LightState << 8) | Result
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
bool postFromIsr(const Event &e);
bool poll(Event &out);
}  // namespace events

}  // namespace core
