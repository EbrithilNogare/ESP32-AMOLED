#include "timer_core.h"

namespace timer {

void Stopwatch::toggle(uint64_t now) {
  if (running_)
    banked_ += now - startedAt_;
  else
    startedAt_ = now;
  running_ = !running_;
}

void Stopwatch::reset() {
  running_ = false;
  banked_ = 0;
}

uint64_t Stopwatch::elapsedMs(uint64_t now) const { return banked_ + (running_ ? now - startedAt_ : 0); }

void Countdown::set(uint32_t seconds) {
  set_ = seconds > kMaxSeconds ? kMaxSeconds : seconds;
  state_ = State::Idle;
}

static uint32_t wrap(int v, int n) { return uint32_t(((v % n) + n) % n); }

void Countdown::stepMinutes(int delta) { set(wrap(int(set_ / 60) + delta, kMaxMinutes + 1) * 60 + set_ % 60); }
void Countdown::stepSeconds(int delta) { set(set_ / 60 * 60 + wrap(int(set_ % 60) + delta, 60)); }

bool Countdown::start(uint64_t now) {
  if (set_ == 0) return false;
  endAt_ = now + uint64_t(set_) * 1000;
  state_ = State::Running;
  return true;
}

void Countdown::toggle(uint64_t now) {
  switch (state_) {
    case State::Idle: start(now); break;
    case State::Running:
      left_ = endAt_ > now ? endAt_ - now : 0;
      state_ = State::Paused;
      break;
    case State::Paused:
      endAt_ = now + left_;
      state_ = State::Running;
      break;
    case State::Done: state_ = State::Idle; break;
  }
}

void Countdown::reset() { state_ = State::Idle; }

uint64_t Countdown::remainingMs(uint64_t now) const {
  switch (state_) {
    case State::Idle: return uint64_t(set_) * 1000;
    case State::Running: return endAt_ > now ? endAt_ - now : 0;
    case State::Paused: return left_;
    case State::Done: break;
  }
  return 0;
}

uint64_t Countdown::overMs(uint64_t now) const { return state_ == State::Done && now > endAt_ ? now - endAt_ : 0; }

bool Countdown::update(uint64_t now) {
  if (state_ != State::Running || now < endAt_) return false;
  state_ = State::Done;
  return true;
}

}  // namespace timer
