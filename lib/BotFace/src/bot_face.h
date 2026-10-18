// Bot face logic: a port of buildBot() in docs/design/ui-mockup.html, with
// every number from docs/apps/bot-face.md. Parameters ease toward targets,
// expressions just set targets, and geometry() turns them into the shapes
// the LVGL adapter draws (screen pixels, 368 x 448).
#pragma once

#include <stdint.h>

namespace bot {

struct Params {
  float open;    // eye openness: 0 closed … 1 normal … 1.25 wide
  float lookX;   // gaze -1 … 1 (left … right)
  float lookY;   // gaze -0.6 … 0.6 (up … down)
  float curve;   // mouth curvature: 0 flat … 0.55 resting … 1 big smile
  float mouthO;  // 0 … 1, > 0.15 draws the "o" mouth instead of the curve
  float bounce;  // whole-face vertical offset in px (negative = up)
  float width;   // eye width multiplier
};

struct Rect {
  float x, y, w, h, r;  // top-left x/y, size, corner radius
};

struct Geometry {
  Rect eyeL, eyeR;
  bool mouthIsO;
  float mx, my;  // mouth centre (curve endpoints are mx±44, my; control point mx, my + 56*curve)
  float curve;
  float oCx, oCy, oRx, oRy;  // "o" mouth ellipse when mouthIsO (radii 0 otherwise)
};

// Shapes for a parameter set. blink multiplies the eye height (1 = open),
// breathe is the vertical breathing offset in px. Exposed for tests and for
// drawing a static face (e.g. a thumbnail).
Geometry computeGeometry(const Params &p, float blink, float breathe);

class Face {
 public:
  explicit Face(uint32_t seed = 1, uint32_t startMs = 0);

  // Scheduling (blink, look, reactions) + easing with the real frame time,
  // so 10, 30 and 60 fps look the same. dimmed → sleepy face, slower blinks.
  void update(uint32_t nowMs, bool dimmed);

  void tap(uint32_t nowMs);  // happy; second tap within 320 ms → surprised

  // Lively mode (docs/apps/bot-face.md, "Idle acts"), off by default so the
  // face matches the mockup exactly: the whole face wanders slowly around
  // the screen (it stays on longer, so nothing sits still on the AMOLED), and
  // every 5-11 s (x 1.8 dimmed) it does something: a wink, a sideways
  // glance, a little hum (two hops), or a yawn (mostly when dimmed). Taps
  // still win. The acts draw from their own random stream, so the blink and
  // look schedule stays the mockup's.
  void setLively(bool on, uint32_t nowMs);

  Geometry geometry(uint32_t nowMs) const;  // includes breathing and blink

  const Params &params() const;
  const Params &targets() const;  // where the easing is heading

 private:
  float rand01();

  uint32_t rng_;
  uint32_t startMs_, lastMs_;
  Params p_, tg_;
  uint32_t nextBlinkMs_, nextLookMs_;
  uint32_t blinkAtMs_ = 0;
  bool blinked_ = false;  // no blink yet → blinkAtMs_ unused
  uint32_t reactUntilMs_ = 0;
  bool reacting_ = false;
  uint32_t hopEndMs_ = 0;  // happy hop: bounce target back to 0 here
  bool hopPending_ = false;
  uint32_t lastTapMs_ = 0;
  bool tapped_ = false;

  enum class Act : uint8_t { None, Wink, Glance, Hum, Yawn };
  float actRand01();
  void startAct(uint32_t nowMs, bool dimmed);
  bool lively_ = false;
  uint32_t actRng_;
  uint32_t nextActMs_ = 0;
  Act act_ = Act::None;
  uint32_t actStartMs_ = 0;
  bool winkLeft_ = false;

 public:
  Act act() const { return act_; }  // for tests
};

}  // namespace bot
