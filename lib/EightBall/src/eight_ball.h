// Magic 8-ball (docs/apps/magic8.md): the answers (a few classics, mostly
// funnier ones, each with its own colour) and a shake detector for the
// accelerometer. No LVGL, no Arduino.
#pragma once

#include <stdint.h>

namespace eightball {

enum class Kind : uint8_t { Yes, Maybe, No };

struct Answer {
  const char *text;  // capitals, '\n' where the line breaks (up to 3 lines)
  Kind kind;
  uint32_t color;  // 0xRRGGBB on black
};

constexpr int kAnswers = 22;  // 10 yes, 6 maybe, 6 no
const Answer &answer(int i);

// An answer index from a random number, never `last` again (-1: any).
int pick(uint32_t random, int last);

struct ShakeConfig {
  float joltG;        // a jolt: |a| differs from 1 g by more than this
  uint8_t jolts;      // this many jolts …
  uint32_t windowMs;  // … within this window start a shake
  uint32_t settleMs;  // calm this long afterwards: the shake is over
};

// Feed it every accelerometer sample (g). A jolt counts when |a| leaves the
// 1 g ± joltG band, and again only after it came back within half of that.
class Shake {
 public:
  enum class Event : uint8_t { None, Started, Settled };

  explicit Shake(const ShakeConfig &c);
  Event update(float ax, float ay, float az, uint32_t nowMs);
  bool shaking() const { return shaking_; }
  void reset();

 private:
  static constexpr int kRing = 8;
  ShakeConfig c_;
  uint32_t jolts_[kRing] = {};
  uint8_t n_ = 0, head_ = 0;
  bool armed_ = true;
  bool shaking_ = false;
  uint32_t lastMotionMs_ = 0;
};

}  // namespace eightball
