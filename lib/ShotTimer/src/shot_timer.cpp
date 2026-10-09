#include "shot_timer.h"

namespace shot {

Zone classify(uint32_t elapsedMs, const Config &c) {
  // Signed: a close window wider than the minimum just means no Early zone.
  const int64_t ms = elapsedMs;
  const int64_t minMs = int64_t(c.targetMinDs) * 100;
  if (ms < minMs - int64_t(c.closeWindowDs) * 100) return Zone::Early;
  if (ms < minMs) return Zone::Close;
  if (ms <= int64_t(c.targetMaxDs) * 100) return Zone::InTarget;
  return Zone::Over;
}

ShotTimer::ShotTimer(const Config &c) : cfg_(c) {}

void ShotTimer::tap(uint32_t nowMs) {
  switch (mode_) {
    case Mode::Ready:
      startMs_ = nowMs;
      mode_ = Mode::Running;
      break;
    case Mode::Running: {
      frozenMs_ = nowMs - startMs_;
      const uint32_t ds = (frozenMs_ + 50) / 100;  // round half up to 0.1 s
      lastDs_ = ds > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(ds);
      record(lastDs_);
      recorded_ = true;
      mode_ = Mode::Done;
      break;
    }
    case Mode::Done: mode_ = Mode::Ready; break;
  }
}

bool ShotTimer::back() {
  if (mode_ != Mode::Done) return false;
  mode_ = Mode::Ready;
  return true;
}

void ShotTimer::discard() {
  if (mode_ != Mode::Done) return;
  if (recorded_ && count_ > 0) {
    head_ = static_cast<uint8_t>((head_ + kHistory - 1) % kHistory);
    count_--;
  }
  recorded_ = false;
  mode_ = Mode::Ready;
}

void ShotTimer::record(uint16_t ds) {
  head_ = static_cast<uint8_t>((head_ + 1) % kHistory);
  ring_[head_] = ds;  // overwrites the oldest once full
  if (count_ < kHistory) count_++;
}

Mode ShotTimer::mode() const { return mode_; }

uint32_t ShotTimer::elapsedMs(uint32_t nowMs) const {
  switch (mode_) {
    case Mode::Running: return nowMs - startMs_;  // wrap-safe
    case Mode::Done: return frozenMs_;
    case Mode::Ready: break;
  }
  return 0;
}

uint16_t ShotTimer::lastShotDs() const { return lastDs_; }
uint8_t ShotTimer::historyCount() const { return count_; }

uint16_t ShotTimer::historyAt(uint8_t i) const {
  if (i >= count_) return 0;
  return ring_[(head_ + kHistory - i) % kHistory];
}

void ShotTimer::loadHistory(const uint16_t *ds, uint8_t count) {
  head_ = 0;
  count_ = 0;
  recorded_ = false;  // the loaded newest isn't "the shot just recorded"
  if (!ds) return;
  if (count > kHistory) count = kHistory;
  // Push oldest first so the newest ends up at the head.
  for (uint8_t i = count; i > 0; i--) record(ds[i - 1]);
}

const Config &ShotTimer::config() const { return cfg_; }

}  // namespace shot
