// Pomodoro timer logic (docs/apps/pomodoro.md). Time is an absolute end
// timestamp while running and a remaining duration while paused, never a
// tick count, so light sleep or a late update() can't make it drift.
#pragma once

#include <stdint.h>

namespace pomo {

enum class Phase : uint8_t { Idle, Focus, ShortBreak, LongBreak };

struct Config {
  uint32_t focusMs, shortMs, longMs;
  uint8_t setSize;     // focus sessions per set before the long break (0 acts as 1)
  bool autoStartNext;  // next phase starts by itself after a phase ends
};

// Compact snapshot for RtcState/NVS. remainingMs is relative, so a restore
// continues from where the save left off, whatever the clock did meanwhile.
struct Saved {
  uint8_t phase;
  uint8_t running;
  uint8_t completed;
  uint32_t remainingMs;
};

class Pomodoro {
 public:
  explicit Pomodoro(const Config &c);

  void toggle(uint64_t nowMs);  // Idle → Focus running; running → paused; paused → running
  void pause(uint64_t nowMs);
  void reset(uint64_t nowMs);   // current phase back to full duration, paused (Idle stays Idle)
  // End the current phase now; it counts like a normal end. A running phase
  // hands over as update() would; a paused one leaves the next phase paused
  // (the user had stopped the clock). No-op in Idle.
  void skip(uint64_t nowMs);

  // Advance if time is up; true if the phase changed. Catches up over any
  // number of phases (e.g. after a long sleep): with autoStartNext each
  // phase starts exactly when the previous one ended, so the overshoot
  // carries over; without it, at most one phase ends and the next waits.
  bool update(uint64_t nowMs);

  Phase phase() const;
  Phase nextPhase() const;  // what comes after the current phase
  bool running() const;
  uint32_t remainingMs(uint64_t nowMs) const;  // Idle reports the focus duration
  uint32_t phaseDurationMs() const;            // Idle reports the focus duration
  uint8_t completed() const;  // focus sessions completed in the current set (0..setSize)

  Saved save(uint64_t nowMs) const;
  void restore(const Saved &s, uint64_t nowMs);

 private:
  uint32_t durationOf(Phase p) const;
  void finishPhase();  // move to nextPhase(), paused at full duration

  Config cfg_;
  Phase phase_ = Phase::Idle;
  bool running_ = false;
  uint8_t completed_ = 0;
  uint64_t endMs_ = 0;        // valid while running
  uint32_t remainingMs_ = 0;  // valid while paused
};

}  // namespace pomo
