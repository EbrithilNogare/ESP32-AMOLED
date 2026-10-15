// Debounced push button with Pressed / Short / Long events. Fed with the raw
// level and a ms clock, so it works the same on GPIO, the IO expander or a
// test (docs/ARCHITECTURE.md §6, §8.3).
#pragma once

#include <stdint.h>

namespace btn {

enum class Event : uint8_t { None, Pressed, Short, Long };

struct Config {
  uint16_t debounceMs;  // raw level must be stable this long to count
  uint16_t longMs;      // held this long (from the debounced press) → Long
};

class Button {
 public:
  explicit Button(const Config &c);

  // Call often (every ~5-10 ms). Returns at most one event per call:
  // Pressed once when the debounced state goes down, Long once when held
  // >= longMs, Short on release when neither Long nor suppress() happened.
  Event update(bool rawPressed, uint32_t nowMs);

  bool held() const;  // debounced state

  // Ignore the current press (e.g. it only woke the screen): no Short/Long
  // until it is released. Also covers a press that is still debouncing.
  void suppress();

 private:
  Config cfg_;
  bool held_ = false;
  bool raw_ = false;
  bool longSent_ = false;
  bool suppressed_ = false;
  uint32_t rawSinceMs_ = 0;   // when raw_ last changed
  uint32_t pressedAtMs_ = 0;  // debounced press time
  bool started_ = false;      // first update() seeds rawSinceMs_
};

}  // namespace btn
