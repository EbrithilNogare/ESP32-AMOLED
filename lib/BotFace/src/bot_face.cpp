#include "bot_face.h"

#include <math.h>

namespace bot {

namespace {

constexpr float kPi = 3.14159265f;
// An update() gap longer than this restarts the schedules from now: the
// signed comparisons below only reach 24.8 days, so a face left without
// update() for longer would otherwise wait another 24.8 days to blink.
constexpr uint32_t kGapMs = 10000;
constexpr Params kResting = {1.0f, 0.0f, 0.0f, 0.55f, 0.0f, 0.0f, 1.0f};

// Timestamps are compared by signed difference so millis() wrap is harmless.
bool after(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) > 0; }
bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

void ease(float &v, float target, float k) { v += (target - v) * k; }

}  // namespace

Geometry computeGeometry(const Params &p, float blink, float breathe) {
  Geometry g;
  const float h = fmaxf(8.0f, 118.0f * p.open * blink);
  const float w = 86.0f * p.width;
  const float r = fminf(28.0f, h / 2);  // a closed eye becomes a pill
  const float cy = 196.0f + 14.0f * p.lookY + breathe + p.bounce;
  const float dx = 18.0f * p.lookX - w / 2;
  g.eyeL = {124.0f + dx, cy - h / 2, w, h, r};
  g.eyeR = {244.0f + dx, cy - h / 2, w, h, r};

  g.mx = 184.0f + 8.0f * p.lookX;
  g.my = 318.0f + breathe + 0.6f * p.bounce;
  g.curve = p.curve;
  g.mouthIsO = p.mouthO > 0.15f;
  if (g.mouthIsO) {
    g.oCx = g.mx;
    g.oCy = g.my + 6.0f;
    g.oRx = 16.0f + 6.0f * p.mouthO;
    g.oRy = 24.0f * p.mouthO;
  } else {
    g.oCx = g.oCy = g.oRx = g.oRy = 0.0f;
  }
  return g;
}

Face::Face(uint32_t seed, uint32_t startMs)
    : rng_(seed ? seed : 0x9E3779B9u),  // xorshift32 is stuck at 0
      startMs_(startMs),
      lastMs_(startMs),
      p_(kResting),
      tg_(kResting),
      nextBlinkMs_(startMs + 1500),
      nextLookMs_(startMs + 1200) {}

float Face::rand01() {
  // xorshift32: tiny and deterministic for a given seed (tests rely on it).
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f);  // [0, 1)
}

void Face::update(uint32_t nowMs, bool dimmed) {
  if (nowMs - lastMs_ > kGapMs) {  // unsigned: also catches a jump backwards
    // Every pending timer is far over (they last at most a few seconds):
    // end reactions and let look and blink fire on this update.
    hopPending_ = false;
    reacting_ = false;
    nextLookMs_ = nowMs - 1;
    nextBlinkMs_ = nowMs - 1;
  }
  if (hopPending_ && reached(nowMs, hopEndMs_)) {
    tg_.bounce = 0;
    hopPending_ = false;
  }
  if (reacting_ && after(nowMs, reactUntilMs_)) reacting_ = false;
  if (!reacting_) {
    tg_.open = dimmed ? 0.45f : 1.0f;
    tg_.curve = dimmed ? 0.25f : 0.55f;
    tg_.mouthO = 0;
    tg_.bounce = 0;
    tg_.width = 1;
  }

  // Same order of random draws as the mockup (centre?, x, y, interval).
  if (after(nowMs, nextLookMs_)) {
    const bool centre = rand01() < 0.3f;
    tg_.lookX = centre ? 0.0f : rand01() * 2 - 1;
    tg_.lookY = centre ? 0.0f : rand01() * 1.2f - 0.6f;
    nextLookMs_ = nowMs + static_cast<uint32_t>(1400 + rand01() * 2200);
  }
  if (after(nowMs, nextBlinkMs_)) {
    blinkAtMs_ = nowMs;
    blinked_ = true;
    const bool dbl = rand01() < 0.2f;
    const float gap = (dbl ? 260.0f : 2000.0f + rand01() * 4000.0f) * (dimmed ? 1.8f : 1.0f);
    nextBlinkMs_ = nowMs + static_cast<uint32_t>(gap);
  }

  // The mockup eases by 0.16 per 60 fps frame; scale that by the real dt.
  int32_t dt = static_cast<int32_t>(nowMs - lastMs_);
  if (dt < 0) dt = 0;
  lastMs_ = nowMs;
  const float k = 1.0f - powf(0.84f, static_cast<float>(dt) / 16.7f);
  ease(p_.open, tg_.open, k);
  ease(p_.lookX, tg_.lookX, k);
  ease(p_.lookY, tg_.lookY, k);
  ease(p_.curve, tg_.curve, k);
  ease(p_.mouthO, tg_.mouthO, k);
  ease(p_.bounce, tg_.bounce, k);
  ease(p_.width, tg_.width, k);
}

void Face::tap(uint32_t nowMs) {
  if (tapped_ && nowMs - lastTapMs_ < 320) {
    tg_.open = 1.25f;
    tg_.width = 1.15f;
    tg_.mouthO = 1;
    tg_.curve = 0;
    tg_.bounce = -6;
    reactUntilMs_ = nowMs + 1300;
    hopPending_ = false;  // keep the surprised -6 instead of the hop's reset
  } else {
    tg_.open = 0.3f;
    tg_.width = 1.08f;
    tg_.curve = 1;
    tg_.mouthO = 0;
    tg_.bounce = -14;
    reactUntilMs_ = nowMs + 900;
    hopEndMs_ = nowMs + 180;
    hopPending_ = true;
  }
  reacting_ = true;
  lastTapMs_ = nowMs;
  tapped_ = true;
}

Geometry Face::geometry(uint32_t nowMs) const {
  float blink = 1.0f;
  const uint32_t sinceBlink = nowMs - blinkAtMs_;
  if (blinked_ && sinceBlink < 150) blink = 1.0f - 0.93f * sinf(kPi * sinceBlink / 150.0f);

  // 15 breathing periods (2π · 900 ms each) are 84823.0 ms to within 0.002 ms,
  // so wrapping there keeps the float argument small without a visible jump.
  const uint32_t t = (nowMs - startMs_) % 84823u;
  const float breathe = 3.0f * sinf(static_cast<float>(t) / 900.0f);
  return computeGeometry(p_, blink, breathe);
}

const Params &Face::params() const { return p_; }
const Params &Face::targets() const { return tg_; }

}  // namespace bot
