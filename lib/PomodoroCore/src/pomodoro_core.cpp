#include "pomodoro_core.h"

namespace pomo {

Pomodoro::Pomodoro(const Config &c) : cfg_(c) {
  if (cfg_.setSize == 0) cfg_.setSize = 1;  // avoids an endless focus run
}

uint32_t Pomodoro::durationOf(Phase p) const {
  switch (p) {
    case Phase::ShortBreak: return cfg_.shortMs;
    case Phase::LongBreak: return cfg_.longMs;
    case Phase::Idle:
    case Phase::Focus: break;
  }
  return cfg_.focusMs;
}

Phase Pomodoro::nextPhase() const {
  switch (phase_) {
    case Phase::Idle: return Phase::Focus;
    case Phase::Focus: return completed_ + 1 >= cfg_.setSize ? Phase::LongBreak : Phase::ShortBreak;
    case Phase::ShortBreak: return Phase::Focus;
    case Phase::LongBreak: break;
  }
  return Phase::Idle;
}

void Pomodoro::finishPhase() {
  const Phase next = nextPhase();
  if (phase_ == Phase::Focus) completed_++;
  if (next == Phase::Idle) completed_ = 0;  // set complete
  phase_ = next;
  running_ = false;
  remainingMs_ = durationOf(next);
}

void Pomodoro::toggle(uint64_t nowMs) {
  if (running_) {
    pause(nowMs);
    return;
  }
  if (phase_ == Phase::Idle) {
    phase_ = Phase::Focus;
    remainingMs_ = cfg_.focusMs;
  }
  running_ = true;
  endMs_ = nowMs + remainingMs_;
}

void Pomodoro::pause(uint64_t nowMs) {
  if (!running_) return;
  remainingMs_ = remainingMs(nowMs);
  running_ = false;
}

// nowMs is unused (reset always pauses, so no end timestamp is needed); kept
// so every state change takes the clock the same way.
void Pomodoro::reset(uint64_t /*nowMs*/) {
  if (phase_ == Phase::Idle) return;
  running_ = false;
  remainingMs_ = durationOf(phase_);
}

void Pomodoro::skip(uint64_t nowMs) {
  if (phase_ == Phase::Idle) return;
  const bool wasRunning = running_;
  finishPhase();
  if (wasRunning && cfg_.autoStartNext && phase_ != Phase::Idle) {
    running_ = true;
    endMs_ = nowMs + remainingMs_;
  }
}

bool Pomodoro::update(uint64_t nowMs) {
  bool changed = false;
  // Bounded: every focus end bumps completed_, so a long break (→ Idle, which
  // stops the loop) comes within 2 * setSize phases even with 0 ms durations.
  while (running_ && nowMs >= endMs_) {
    const uint64_t endedAt = endMs_;
    finishPhase();
    changed = true;
    if (!cfg_.autoStartNext || phase_ == Phase::Idle) break;
    // Start from the scheduled end, not from now, so the overshoot carries over.
    running_ = true;
    endMs_ = endedAt + remainingMs_;
  }
  return changed;
}

Phase Pomodoro::phase() const { return phase_; }
bool Pomodoro::running() const { return running_; }
uint8_t Pomodoro::completed() const { return completed_; }
uint32_t Pomodoro::phaseDurationMs() const { return durationOf(phase_); }

uint32_t Pomodoro::remainingMs(uint64_t nowMs) const {
  if (phase_ == Phase::Idle) return cfg_.focusMs;
  if (!running_) return remainingMs_;
  if (nowMs >= endMs_) return 0;
  const uint64_t left = endMs_ - nowMs;
  return left > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(left);
}

Saved Pomodoro::save(uint64_t nowMs) const {
  Saved s;
  s.phase = static_cast<uint8_t>(phase_);
  s.running = running_ ? 1 : 0;
  s.completed = completed_;
  s.remainingMs = remainingMs(nowMs);
  return s;
}

void Pomodoro::restore(const Saved &s, uint64_t nowMs) {
  // Blobs come from RTC memory/NVS: clamp anything out of range (e.g. a
  // config change between save and restore) instead of trusting it.
  phase_ = s.phase <= static_cast<uint8_t>(Phase::LongBreak) ? static_cast<Phase>(s.phase) : Phase::Idle;
  // Focus/ShortBreak run with at most setSize - 1 sessions done (the last
  // focus end goes to LongBreak); only LongBreak may hold setSize. Anything
  // higher would make the next focus end count past the set.
  const uint8_t maxDone = phase_ == Phase::LongBreak ? cfg_.setSize : static_cast<uint8_t>(cfg_.setSize - 1);
  completed_ = s.completed <= maxDone ? s.completed : maxDone;
  const uint32_t full = durationOf(phase_);
  remainingMs_ = s.remainingMs <= full ? s.remainingMs : full;
  running_ = false;
  if (phase_ == Phase::Idle) {
    completed_ = 0;
    return;
  }
  if (s.running) {
    running_ = true;
    endMs_ = nowMs + remainingMs_;
  }
}

}  // namespace pomo
