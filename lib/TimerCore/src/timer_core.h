// Stopwatch and countdown for the Timer app (docs/apps/timer.md). Pure
// logic: the caller passes a 64-bit millisecond clock (core::extend), so
// light sleep and millis() wrapping can't make them drift.
#pragma once

#include <stdint.h>

namespace timer {

constexpr uint32_t kMaxMinutes = 99;
constexpr uint32_t kMaxSeconds = kMaxMinutes * 60 + 59;  // 99:59

class Stopwatch {
 public:
  void toggle(uint64_t now);  // start or pause
  void reset();
  bool running() const { return running_; }
  bool zero() const { return !running_ && banked_ == 0; }
  uint64_t elapsedMs(uint64_t now) const;

 private:
  bool running_ = false;
  uint64_t startedAt_ = 0;  // while running
  uint64_t banked_ = 0;     // time from earlier runs
};

enum class State : uint8_t { Idle, Running, Paused, Done };

class Countdown {
 public:
  // The set time, 0..kMaxSeconds. Changing it stops a run (back to Idle).
  void set(uint32_t seconds);
  uint32_t seconds() const { return set_; }
  // Wheel steps: minutes wrap within 0..99, seconds within 0..59, no carry.
  void stepMinutes(int delta);
  void stepSeconds(int delta);

  bool start(uint64_t now);   // false (and Idle) when the set time is 0:00
  void toggle(uint64_t now);  // Idle/Paused → Running, Running → Paused, Done → Idle
  void reset();               // back to Idle; the set time stays
  State state() const { return state_; }
  bool running() const { return state_ == State::Running; }

  uint64_t remainingMs(uint64_t now) const;  // the set time while Idle, 0 once Done
  uint64_t overMs(uint64_t now) const;       // time since it reached zero (Done only)
  // Call often: Running → Done when the time is up. True on that step only.
  bool update(uint64_t now);

 private:
  uint32_t set_ = 5 * 60;
  State state_ = State::Idle;
  uint64_t endAt_ = 0;  // Running; the moment it reached zero once Done
  uint64_t left_ = 0;   // Paused
};

}  // namespace timer
