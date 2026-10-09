// Burn-in pixel shift (docs/ARCHITECTURE.md §7): the whole UI moves along a
// slow Lissajous path, one step per PIXEL_SHIFT_PERIOD_MS.
#pragma once

#include <stdint.h>

namespace burnin {

struct Offset {
  int8_t x, y;
};

// Offset for a step. Deterministic, |x|,|y| <= maxPx, step 0 → (0, 0).
// The two axes run at frequencies with an irrational ratio, so the path
// never settles into a short repeating loop, and consecutive steps move at
// most 2 px per axis whatever maxPx is. maxPx <= 0 → always (0, 0).
Offset pixelShift(uint32_t step, int8_t maxPx);

}  // namespace burnin
