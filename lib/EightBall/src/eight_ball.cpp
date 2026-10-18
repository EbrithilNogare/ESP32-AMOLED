#include "eight_ball.h"

#include <math.h>

namespace eightball {

namespace {

const Answer kTable[kAnswers] = {
    {"IT IS\nCERTAIN", Kind::Yes},
    {"IT IS\nDECIDEDLY SO", Kind::Yes},
    {"WITHOUT\nA DOUBT", Kind::Yes},
    {"YES,\nDEFINITELY", Kind::Yes},
    {"YOU MAY\nRELY ON IT", Kind::Yes},
    {"AS I SEE IT,\nYES", Kind::Yes},
    {"MOST\nLIKELY", Kind::Yes},
    {"OUTLOOK\nGOOD", Kind::Yes},
    {"YES", Kind::Yes},
    {"SIGNS POINT\nTO YES", Kind::Yes},
    {"REPLY HAZY,\nTRY AGAIN", Kind::Maybe},
    {"ASK AGAIN\nLATER", Kind::Maybe},
    {"BETTER NOT\nTELL YOU NOW", Kind::Maybe},
    {"CANNOT\nPREDICT NOW", Kind::Maybe},
    {"CONCENTRATE\nAND ASK AGAIN", Kind::Maybe},
    {"DON'T\nCOUNT ON IT", Kind::No},
    {"MY REPLY\nIS NO", Kind::No},
    {"MY SOURCES\nSAY NO", Kind::No},
    {"OUTLOOK NOT\nSO GOOD", Kind::No},
    {"VERY\nDOUBTFUL", Kind::No},
};

}  // namespace

const Answer &answer(int i) { return kTable[i >= 0 && i < kAnswers ? i : 0]; }

int pick(uint32_t random, int last) {
  if (last < 0 || last >= kAnswers) return int(random % kAnswers);
  // One of the other 19, uniformly: skip over `last`. (random % 19 is
  // biased by under 1e-8: fine for a toy.)
  const int i = int(random % (kAnswers - 1));
  return i >= last ? i + 1 : i;
}

Shake::Shake(const ShakeConfig &c) : c_(c) {}

void Shake::reset() {
  n_ = head_ = 0;
  armed_ = true;
  shaking_ = false;
}

Shake::Event Shake::update(float ax, float ay, float az, uint32_t nowMs) {
  const float dev = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  if (dev > c_.joltG * 0.5f) lastMotionMs_ = nowMs;
  Event ev = Event::None;
  if (armed_ && dev > c_.joltG) {
    armed_ = false;
    jolts_[head_] = nowMs;
    head_ = uint8_t((head_ + 1) % kRing);
    if (n_ < kRing) n_++;
    if (!shaking_ && n_ >= c_.jolts) {
      // The jolts-th most recent jolt within the window?
      const uint32_t first = jolts_[(head_ + kRing - c_.jolts) % kRing];
      if (nowMs - first <= c_.windowMs) {
        shaking_ = true;
        ev = Event::Started;
      }
    }
  } else if (!armed_ && dev < c_.joltG * 0.5f) {
    armed_ = true;
  }
  if (shaking_ && ev == Event::None && nowMs - lastMotionMs_ >= c_.settleMs) {
    shaking_ = false;
    n_ = 0;
    ev = Event::Settled;
  }
  return ev;
}

}  // namespace eightball
