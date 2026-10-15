// Spirit-level maths for the Level app (docs/apps/level.md), free of Arduino
// and LVGL so it runs in the host tests.
//
// Device frame: x points to the right edge of the screen, y to its top edge,
// z out of the screen toward the viewer. The accelerometer reports the
// "up" direction: about (0, 0, +1 g) with the board lying face up.
#pragma once

#include <stdint.h>

namespace level {

constexpr float kLevelDeg = 0.3f;  // green
constexpr float kNearDeg = 2.0f;   // yellow
constexpr float kRefHysteresisDeg = 2.0f;

struct Accel {
  float x, y, z;
};

enum class View : uint8_t { Flat, Edge };
enum class State : uint8_t { Level, Near, Far };

State stateOf(float deviationDeg);

// Angle between the screen normal and up: 0 face up, 90 on an edge, 180 face down.
float tiltFromFlat(const Accel &a);
// Flat below 40°, edge above 50°; in between the current view stays.
View chooseView(View current, const Accel &a);

// Lying flat. x > 0: the right side is low; y > 0: the bottom (front) side is
// low. The bubble sits on the high side.
struct Flat {
  float x, y;
};
Flat flatAngles(const Accel &a);

// Standing on an edge. rotation: angle in the screen plane, 0 = standing on the
// bottom edge, positive clockwise as seen by the viewer, -180..180.
// lean: how far the screen tips toward the sky (> 0) or the floor (< 0).
struct Edge {
  float rotation, lean;
};
Edge edgeAngles(const Accel &a);

// The nearest reference for a rotation: level (0 / 180) or plumb (±90), and
// the signed deviation from it. With `prev` (the last result) the reference
// only changes once another one is closer by kRefHysteresisDeg, so the
// readout doesn't flip between level and plumb around ±45°.
struct Ref {
  float ref, dev;
  bool plumb;
};
Ref nearestRef(float rotationDeg, const Ref *prev = nullptr);

// Landscape while standing on the left edge (rotation ≈ −90°; the right edge
// carries the buttons, so the board can't stand on it): entered within
// kLandscapeInDeg of it, left beyond kLandscapeOutDeg or when lying flat.
constexpr float kLandscapeInDeg = 35.0f;
constexpr float kLandscapeOutDeg = 50.0f;
bool landscapeFor(bool current, View view, float rotationDeg);

float wrap180(float deg);
float percentSlope(float deg);  // tan(deg) × 100, clamped to ±9999
// Bubble distance from the vial centre: linear near level, saturating toward
// the rim (R · tanh(|deg| / 7)).
float bubbleRadius(float deg, float maxPx);

// Inverse helpers (simulator and tests): the gravity vector for given angles.
Accel fromFlat(float xDeg, float yDeg);
Accel fromEdge(float rotationDeg, float leanDeg);

// Exponential low-pass on the gravity vector; the first sample passes through.
class Smoother {
 public:
  explicit Smoother(float alpha) : alpha_(alpha) {}
  const Accel &push(const Accel &a);
  const Accel &value() const { return s_; }
  void reset() { primed_ = false; }

 private:
  float alpha_;
  bool primed_ = false;
  Accel s_{0, 0, 1};
};

}  // namespace level
