#pragma once
#include <stdint.h>

// Dice logic for the Dice app (docs/apps/dice.md), free of Arduino and LVGL so
// it runs in the host tests: rolls with advantage/disadvantage and percentile
// d100, the wireframe solids with their face-on rest pose, the spin that
// decays exactly onto that pose, projection, the 2D die icons and the grid
// layout of several dice.
namespace dice {

constexpr int kTypes = 7;
constexpr uint8_t kDice[kTypes] = {4, 6, 8, 10, 12, 20, 100};
constexpr int kMaxCount = 10;              // dice per throw
constexpr int kMaxCountD100 = 4;           // d100 is two d10s each
constexpr int kMaxPieces = 2 * kMaxCount;  // drawn solids per throw (both sets)

enum class Mode : uint8_t { Flat, Adv, Dis };
enum class Crit : uint8_t { None, Nat20, Nat1 };

int maxCount(uint8_t die);
bool validDie(uint8_t die);

// Uniform 1..n from a 32-bit random source, without modulo bias.
using Rng = uint32_t (*)();
uint8_t uniform(Rng rng, uint8_t n);

struct Roll {
  uint8_t die = 20, count = 1;
  Mode mode = Mode::Flat;
  uint8_t sets = 1;              // 2 with advantage/disadvantage
  uint8_t v[2][kMaxCount] = {};  // results, 1..die
  uint16_t total[2] = {};
  uint8_t kept = 0;        // index of the set that counts
  Crit crit = Crit::None;  // single d20 only
};
// Advantage keeps the higher total, disadvantage the lower; a tie keeps the first.
Roll roll(uint8_t die, uint8_t count, Mode mode, Rng rng);
// The resting view before the first roll: every die shows its maximum.
Roll idle(uint8_t die, uint8_t count, Mode mode);

// Percentile dice: 1..100 -> tens die 0..90 and ones die 0..9 (00 + 0 = 100).
struct Percentile {
  uint8_t tens, ones;
};
Percentile split(uint8_t value);

// ---------------------------------------------------------------- 3D

struct Vec3 {
  float x, y, z;
};
struct Mat3 {
  float m[3][3];
};
Vec3 mul(const Mat3 &a, const Vec3 &v);
Mat3 mul(const Mat3 &a, const Mat3 &b);
Mat3 axisAngle(const Vec3 &unitAxis, float radians);
Vec3 randomAxis(float u1, float u2);  // u1, u2 uniform in [0, 1)
float easeOutQuart(float t);

constexpr int kMaxVerts = 20, kMaxEdges = 30, kMaxFaceVerts = 5;

// Unit-radius solid. y points down the screen and the viewer looks along +z,
// so a face is "toward the viewer" when its normal is (0, 0, -1).
struct Solid {
  uint8_t die;  // 4, 6, 8, 10, 12, 20 (d100 draws two d10s)
  uint8_t nv, ne, nface;
  Vec3 v[kMaxVerts];
  uint8_t e[kMaxEdges][2];
  Mat3 rest;                    // turns `face` straight at the viewer, upright
  uint8_t face[kMaxFaceVerts];  // that face's vertices, in drawing order
};
const Solid &solid(uint8_t die);

// Orientation while spinning: rest × rotation(axis, turns × (1 − ease(p))).
// At p >= 1 it returns `rest` exactly, so every roll lands perfectly face-on.
Mat3 spinPose(const Solid &s, const Vec3 &axis, float turnsRadians, float progress);

struct Point {
  float x, y, z;  // screen position and depth (negative = toward the viewer)
};
// Perspective projection, centre (cx, cy), radius r in pixels.
void project(const Solid &s, const Mat3 &pose, float cx, float cy, float r, Point *out);

// ---------------------------------------------------------------- 2D

// Flat die icon (outline plus faint inner lines) in unit coordinates.
struct Segment {
  float x1, y1, x2, y2;
  bool inner;
};
constexpr int kMaxIconSegments = 40;
int icon(uint8_t die, Segment *out);  // returns the segment count

struct Slot {
  float x, y, r;
};
// Lays n dice out in the rect (x, y, w, h) on the grid with the biggest cells;
// `aspect` = cell width / height (d100 needs wider cells). Returns the radius.
float layoutGrid(int n, float x, float y, float w, float h, float maxR, float aspect, Slot *out);

}  // namespace dice
