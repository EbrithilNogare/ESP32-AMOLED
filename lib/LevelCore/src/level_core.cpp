#include "level_core.h"

#include <math.h>

namespace level {

static constexpr float kR2D = 57.2957795f;
static constexpr float kD2R = 1.0f / kR2D;

State stateOf(float dev) {
  const float d = fabsf(dev);
  return d <= kLevelDeg ? State::Level : d <= kNearDeg ? State::Near : State::Far;
}

float tiltFromFlat(const Accel &a) {
  const float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (n <= 0) return 0;
  float c = a.z / n;
  c = c > 1 ? 1 : c < -1 ? -1 : c;
  return acosf(c) * kR2D;
}

View chooseView(View current, const Accel &a) {
  float t = tiltFromFlat(a);
  if (t > 90) t = 180 - t;  // face down counts as flat too
  if (current == View::Flat) return t > 50 ? View::Edge : View::Flat;
  return t < 40 ? View::Flat : View::Edge;
}

Flat flatAngles(const Accel &a) {
  const float z = fabsf(a.z);
  return {atan2f(-a.x, z) * kR2D, atan2f(a.y, z) * kR2D};
}

Edge edgeAngles(const Accel &a) { return {atan2f(-a.x, a.y) * kR2D, atan2f(a.z, sqrtf(a.x * a.x + a.y * a.y)) * kR2D}; }

float wrap180(float d) {
  d = fmodf(d + 180.0f, 360.0f);
  if (d < 0) d += 360.0f;
  return d - 180.0f;
}

Ref nearestRef(float rotation, const Ref *prev) {
  const float n = wrap180(rotation);
  static const float kRefs[4] = {0, 90, -90, 180};
  Ref best = {0, 1e9f, false};
  for (float r : kRefs) {
    const float d = wrap180(n - r);
    if (fabsf(d) < fabsf(best.dev)) best = {r, d, fabsf(r) == 90};
  }
  if (prev && prev->ref != best.ref) {
    const float d = wrap180(n - prev->ref);
    if (fabsf(d) < fabsf(best.dev) + kRefHysteresisDeg) return {prev->ref, d, prev->plumb};
  }
  return best;
}

bool landscapeFor(bool current, View view, float rotation) {
  if (view != View::Edge) return false;
  const float d = fabsf(wrap180(rotation + 90.0f));
  return current ? d <= kLandscapeOutDeg : d <= kLandscapeInDeg;
}

float percentSlope(float deg) {
  const float p = tanf(deg * kD2R) * 100.0f;
  return p > 9999 ? 9999 : p < -9999 ? -9999 : p;
}

float bubbleRadius(float deg, float maxPx) { return maxPx * tanhf(fabsf(deg) / 7.0f); }

Accel fromFlat(float xDeg, float yDeg) {
  const float ax = -tanf(xDeg * kD2R), ay = tanf(yDeg * kD2R), n = sqrtf(ax * ax + ay * ay + 1);
  return {ax / n, ay / n, 1 / n};
}

Accel fromEdge(float rotationDeg, float leanDeg) {
  const float r = rotationDeg * kD2R, l = leanDeg * kD2R;
  return {-sinf(r) * cosf(l), cosf(r) * cosf(l), sinf(l)};
}

const Accel &Smoother::push(const Accel &a) {
  if (!primed_) {
    s_ = a;
    primed_ = true;
  } else {
    s_.x += (a.x - s_.x) * alpha_;
    s_.y += (a.y - s_.y) * alpha_;
    s_.z += (a.z - s_.z) * alpha_;
  }
  return s_;
}

}  // namespace level
