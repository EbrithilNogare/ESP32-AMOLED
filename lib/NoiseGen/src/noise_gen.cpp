#include "noise_gen.h"

#include <math.h>

namespace noise {

static constexpr float kPi = 3.14159265f;
static constexpr float kDropsPerSecond = 28;

const char *name(Kind k) {
  switch (k) {
    case Kind::White: return "White noise";
    case Kind::Blue: return "Blue noise";
    case Kind::Rain: break;
  }
  return "Rain";
}

Generator::Generator(uint32_t seed, uint32_t sampleRate)
    : rng_(seed ? seed : 1),
      fs_(float(sampleRate)),
      // Bed: pink noise, low-passed (~3 kHz) and high-passed (~150 Hz).
      lpA_(1 - expf(-2 * kPi * 3000 / fs_)),
      hpA_(expf(-2 * kPi * 150 / fs_)),
      dropChance_(kDropsPerSecond / fs_) {}

float Generator::white() {
  // xorshift32: fast, and plenty random for noise.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(int32_t(rng_)) * (1.0f / 2147483648.0f);
}

static float rand01(float w) { return 0.5f + 0.5f * w; }

float Generator::pink() {
  const float w = white();
  p_[0] = 0.99886f * p_[0] + w * 0.0555179f;
  p_[1] = 0.99332f * p_[1] + w * 0.0750759f;
  p_[2] = 0.96900f * p_[2] + w * 0.1538520f;
  p_[3] = 0.86650f * p_[3] + w * 0.3104856f;
  p_[4] = 0.55000f * p_[4] + w * 0.5329522f;
  p_[5] = -0.7616f * p_[5] - w * 0.0168980f;
  const float out = p_[0] + p_[1] + p_[2] + p_[3] + p_[4] + p_[5] + p_[6] + w * 0.5362f;
  p_[6] = w * 0.115926f;
  return out * 0.11f;
}

// Blue noise rises 3 dB per octave: the first difference of pink noise
// (pink falls 3 dB/octave, differencing adds 6).
float Generator::blue() {
  const float p = pink();
  const float out = p - bluePrev_;
  bluePrev_ = p;
  return out;
}

// Rain: a filtered-noise bed that swells in slow gusts, plus random drops,
// each a short decaying tick, some with a rising "plink" tone.
float Generator::rain() {
  bedLp_ += lpA_ * (pink() - bedLp_);
  bedHp_ = hpA_ * bedHp_ + (1 - hpA_) * bedLp_;
  if (gustLeft_ == 0) {
    gustTarget_ = 0.65f + 0.35f * rand01(white());
    gustLeft_ = uint32_t(fs_ * (1.5f + 2.5f * rand01(white())));
  }
  gustLeft_--;
  gust_ += (gustTarget_ - gust_) * (1.0f / (0.8f * fs_));
  float s = (bedLp_ - bedHp_) * 1.6f * gust_;

  // New drop: mostly small and close together, now and then a big one.
  if (rand01(white()) < dropChance_ * gust_) {
    for (Drop &d : drops_) {
      if (d.amp > 0.002f) continue;
      const float size = rand01(white());
      d.amp = 0.05f + 0.5f * size * size * size;
      d.decay = expf(-1.0f / (fs_ * (0.002f + 0.010f * rand01(white()))));
      d.phase = 0;
      d.step = 2 * kPi * (900 + 2600 * rand01(white())) / fs_;
      d.chirp = 1 + 0.00012f * rand01(white());  // drops rise in pitch
      d.tone = rand01(white()) < 0.3f ? 0.7f : 0.15f;
      break;
    }
  }
  // The ticks share one high-passed noise source.
  const float w = white();
  tickHp_ = 0.6f * (tickHp_ + w - tickPrev_);
  tickPrev_ = w;
  for (Drop &d : drops_) {
    if (d.amp <= 0.002f) continue;
    s += d.amp * (d.tone * sinf(d.phase) + (1 - d.tone) * tickHp_);
    d.phase += d.step;
    if (d.phase > 2 * kPi) d.phase -= 2 * kPi;
    d.step *= d.chirp;
    d.amp *= d.decay;
  }
  return s;
}

// Soft limiter: transparent below ±0.5, never past ±1.
static float soft(float x) {
  const float a = fabsf(x);
  if (a <= 0.5f) return x;
  const float y = 0.5f + 0.5f * tanhf((a - 0.5f) * 2);
  return x < 0 ? -y : y;
}

void Generator::fill(Kind kind, int16_t *out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    float s;
    switch (kind) {
      case Kind::White: s = white() * 0.24f; break;
      case Kind::Blue: s = blue() * 0.95f; break;
      case Kind::Rain:
      default: s = rain(); break;
    }
    out[i] = int16_t(lroundf(soft(s) * 32767));
  }
}

}  // namespace noise
