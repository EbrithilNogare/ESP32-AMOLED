// Touch calibration (docs/apps/system.md): maps the touch controller's raw
// position to screen pixels with a scale and an offset per axis, fitted from
// a few targets the user taps.
#pragma once

#include <stdint.h>

namespace touchcal {

// screen = raw * scale / 65536 + offset, per axis. The default is identity.
struct Cal {
  int32_t xScale = 65536, xOffset = 0;
  int32_t yScale = 65536, yOffset = 0;
};

struct Sample {
  int16_t rawX, rawY;  // what the controller reported
  int16_t x, y;        // the target the user was asked to tap
};

int16_t mapX(const Cal &c, int16_t raw);
int16_t mapY(const Cal &c, int16_t raw);

// A correction small enough to be the panel's error, not a mistake: scale
// within ±20 %, offset within ±kMaxOffset px.
constexpr int32_t kMaxOffset = 64;
bool plausible(const Cal &c);

// Least-squares fit over `n` samples (at least 2, spread out on both axes).
// False if they don't determine both axes, the result isn't plausible, or a
// sample misses its fitted target by more than maxMissPx (a sloppy tap).
bool fit(const Sample *s, int n, int32_t maxMissPx, Cal &out);

}  // namespace touchcal
