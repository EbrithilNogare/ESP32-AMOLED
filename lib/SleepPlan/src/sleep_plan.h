// How deep the device sleeps once its screen is off (power button or idle
// timeout), from what the apps need (docs/ARCHITECTURE.md §8). Pure logic:
// core::power asks every app through apps::manager, merges the answers and
// lets this decide; it owns the actual light and deep sleep.
#pragma once

#include <stdint.h>

namespace sleepplan {

// What one app needs while the screen is off. Times are on the monotonic
// clock that keeps counting through deep sleep (core::mono()).
struct Need {
  bool awake;         // the chip must keep running: sound plays (Noise)
  uint64_t wakeAtMs;  // run again at this time (a countdown ends), 0 = never
};

// Both apps' needs: awake if either is, the earlier wake time.
Need merge(const Need &a, const Need &b);

enum class Kind : uint8_t {
  Sleep,     // power button press or idle timeout: the apps keep working
  PowerOff,  // power button held: everything stops, only the button wakes
};

// Doze: the CPU stays up with the screen and touch off, light-sleeping
// between loop passes; waking is instant. Deep: ESP32 deep sleep, a wake is
// a reboot that restores the last app.
enum class Mode : uint8_t { Doze, Deep };

struct Config {
  uint32_t minDeepMs;   // a deadline sooner than this isn't worth a deep sleep (boot cost)
  uint32_t wakeLeadMs;  // wake this much early: booting takes a while
  uint32_t dozePollMs;  // longest light sleep while dozing (charger and battery checks)
};

struct Plan {
  Mode mode;
  // Deep: timer wake after this many ms, 0 = only the power button wakes.
  // Doze: the longest light sleep before the loop runs again (0 = due now).
  uint64_t ms;
};

// Sound or the charger keep the chip dozing; so does a deadline closer than
// minDeepMs + wakeLeadMs. Otherwise deep sleep, with a timer for the
// earliest deadline. PowerOff ignores the apps: deep sleep, no timer.
Plan decide(const Need &need, uint64_t nowMs, bool onCharger, Kind kind, const Config &c);

}  // namespace sleepplan
