// Procedural sounds for the Noise app (docs/apps/noise.md): rain, white
// noise and blue noise, generated sample by sample (no recordings in flash).
// Pure logic, so it is tested on the PC: loudness, spectrum tilt, no clipping.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace noise {

enum class Kind : uint8_t { Rain, White, Blue };
constexpr int kKinds = 3;

const char *name(Kind k);  // "Rain", "White noise", "Blue noise"
inline Kind next(Kind k, int step) { return Kind(((int(k) + step) % kKinds + kKinds) % kKinds); }

class Generator {
 public:
  explicit Generator(uint32_t seed = 1, uint32_t sampleRate = 16000);
  // Appends n mono samples of `kind`. All kinds come out at about the same
  // loudness, so swiping between them doesn't jump in volume.
  void fill(Kind kind, int16_t *out, size_t n);

 private:
  struct Drop {
    float amp = 0, decay = 0;  // per-sample envelope multiplier
    float phase = 0, step = 0, chirp = 0;
    float tone = 0;  // share of the sine "plink" vs. the noise tick
  };
  static constexpr int kDrops = 10;

  float white();  // uniform -1..1
  float pink();   // Paul Kellet's filter, about -1..1
  float rain();
  float blue();

  uint32_t rng_;
  float fs_;
  float p_[7] = {};              // pink filter state
  float bluePrev_ = 0;           // last pink sample, for the blue difference
  float lpA_, hpA_;              // rain bed filter coefficients
  float bedLp_ = 0, bedHp_ = 0;  // rain bed filters
  float gust_ = 1, gustTarget_ = 1;
  uint32_t gustLeft_ = 0;  // samples until the next gust target
  float tickHp_ = 0, tickPrev_ = 0;
  float dropChance_;  // per sample
  Drop drops_[kDrops];
};

}  // namespace noise
