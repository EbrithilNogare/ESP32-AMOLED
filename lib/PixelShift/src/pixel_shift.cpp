#include "pixel_shift.h"

#include <math.h>

namespace burnin {

namespace {

// Peak speed of an axis is maxPx * freq px per step; keeping that at 1.2
// means the rounded offsets change by at most 2 px between steps.
constexpr double kSpeedX = 1.2;
constexpr double kSpeedY = 1.2 / 1.6180339887;  // golden ratio: never in step with x

int8_t axis(double phase, int8_t maxPx) { return static_cast<int8_t>(lround(maxPx * sin(phase))); }

}  // namespace

Offset pixelShift(uint32_t step, int8_t maxPx) {
  if (maxPx <= 0) return {0, 0};
  // Double: step * freq must stay precise for billions of steps. Called once
  // every few minutes, so the soft-float cost on the ESP32-S3 doesn't matter.
  const double s = static_cast<double>(step);
  return {axis(s * kSpeedX / maxPx, maxPx), axis(s * kSpeedY / maxPx, maxPx)};
}

}  // namespace burnin
