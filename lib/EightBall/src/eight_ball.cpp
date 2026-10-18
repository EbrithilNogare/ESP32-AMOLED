#include "eight_ball.h"

#include <math.h>

namespace eightball {

namespace {

const Answer kTable[kAnswers] = {
    {"IT IS\nCERTAIN", Kind::Yes, 0xEDEDED},
    {"WITHOUT\nA DOUBT", Kind::Yes, 0x4CD07D},
    {"YES.\nOBVIOUSLY.", Kind::Yes, 0xFFC93C},
    {"SIGNS POINT\nTO YES", Kind::Yes, 0x00E5FF},
    {"THE STARS\nSAY YES", Kind::Yes, 0xB388FF},
    {"HELL\nYES!", Kind::Yes, 0xFF6B3D},
    {"DO IT.\nNO REGRETS.", Kind::Yes, 0xFF8A1F},
    {"MOST\nLIKELY", Kind::Yes, 0xEDEDED},
    {"YES, BUT\nWEAR A\nHELMET", Kind::Yes, 0xFFD54F},
    {"100 %.\nTRUST ME.", Kind::Yes, 0x4CD07D},
    {"ASK AGAIN\nLATER", Kind::Maybe, 0x9A9A9A},
    {"REPLY HAZY,\nTRY AGAIN", Kind::Maybe, 0xB388FF},
    {"ASK YOUR\nCAT", Kind::Maybe, 0xFF6FA8},
    {"NEED\nCOFFEE\nFIRST", Kind::Maybe, 0xC8915A},
    {"CONCENTRATE\nAND ASK\nAGAIN", Kind::Maybe, 0x6E8BFF},
    {"FLIP A COIN.\nI'M BUSY.", Kind::Maybe, 0xFFB020},
    {"DON'T COUNT\nON IT", Kind::No, 0xFF5A4E},
    {"NOPE.", Kind::No, 0xFF3B3B},
    {"VERY\nDOUBTFUL", Kind::No, 0xFF8A80},
    {"NOT IN THIS\nTIMELINE", Kind::No, 0xE040FB},
    {"ABSOLUTELY\nNOT", Kind::No, 0xFF5A4E},
    {"MY SOURCES\nSAY NO", Kind::No, 0xFF7043},
};

}  // namespace

const Answer &answer(int i) { return kTable[i >= 0 && i < kAnswers ? i : 0]; }

int pick(uint32_t random, int last) {
  if (last < 0 || last >= kAnswers) return int(random % kAnswers);
  // One of the others, uniformly: skip over `last`. (The modulo bias is
  // under 1e-8: fine for a toy.)
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
