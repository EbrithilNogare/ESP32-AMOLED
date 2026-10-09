// Espresso shot stopwatch (docs/apps/espresso-timer.md): Ready → Running →
// Done, a colour zone for the elapsed time, and the last kHistory shots.
#pragma once

#include <stdint.h>

namespace shot {

enum class Mode : uint8_t { Ready, Running, Done };
enum class Zone : uint8_t { Early, Close, InTarget, Over };

// Deciseconds, e.g. 250, 300, 50 for a 25-30 s window, amber from 20 s.
struct Config {
  uint16_t targetMinDs, targetMaxDs, closeWindowDs;
};

// < min - close → Early; < min → Close; <= max → InTarget; else Over.
Zone classify(uint32_t elapsedMs, const Config &c);

constexpr uint8_t kHistory = 20;

class ShotTimer {
 public:
  explicit ShotTimer(const Config &c);

  void tap(uint32_t nowMs);  // Ready→Running (start), Running→Done (stop, record), Done→Ready
  bool back();               // Done→Ready returns true; otherwise false (caller leaves the app)
  void discard();            // in Done: drop the shot just recorded, go to Ready

  Mode mode() const;
  uint32_t elapsedMs(uint32_t nowMs) const;  // live while Running, frozen in Done, 0 in Ready
  uint16_t lastShotDs() const;               // rounded to 0.1 s at stop; 0 before the first
  uint8_t historyCount() const;
  uint16_t historyAt(uint8_t i) const;       // 0 = newest, deciseconds; 0 if out of range
  void loadHistory(const uint16_t *ds, uint8_t count);  // newest first, extra entries ignored
  const Config &config() const;

 private:
  void record(uint16_t ds);

  Config cfg_;
  Mode mode_ = Mode::Ready;
  uint32_t startMs_ = 0;
  uint32_t frozenMs_ = 0;
  uint16_t lastDs_ = 0;
  bool recorded_ = false;  // the Done shot is still in the history (discard can drop it)
  uint16_t ring_[kHistory] = {};
  uint8_t head_ = 0;  // index of the newest entry
  uint8_t count_ = 0;
};

}  // namespace shot
