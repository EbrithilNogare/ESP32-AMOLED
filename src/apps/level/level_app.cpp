#include "level_app.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "config.h"
#include "core/imu.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/draw.h"
#include "ui/theme.h"

namespace apps {

namespace draw = ui::draw;
using level::State;
using level::View;

static const AppInfo kInfo = {"level", "Level", ICON_STRAIGHTEN, LEVEL_COLOR};

static constexpr int32_t kStageH = 376;   // everything above the bottom bar
static constexpr uint32_t kFrameMs = 33;  // at most 30 redraws a second
static constexpr float kD2R = 0.01745329f;
static constexpr uint32_t kVial = 0x0B0A06, kVialRim = 0x2E2A1C, kRings = 0x6B5E2E;
static constexpr uint32_t kSky = 0x08101A, kGround = 0x1C1604;

LevelApp::LevelApp() : filter_(0.2f) {}

const AppInfo &LevelApp::info() const { return kInfo; }
uint32_t LevelApp::idleDimMs() const { return LEVEL_IDLE_DIM_MS; }
bool LevelApp::zeroed() const { return shown_.view == View::Flat ? saved_.flatZeroed : saved_.edgeZeroed; }

static uint32_t stateColor(float dev) {
  switch (level::stateOf(dev)) {
    case State::Level: return ui::col::success;
    case State::Near: return LEVEL_COLOR;
    default: return ui::col::text;
  }
}

// "+2.4°" / "−1.2°" or the percent slope; `sign` = false for magnitudes.
void LevelApp::fmt(char *buf, size_t n, float deg, bool sign) const {
  const float v = saved_.percent ? level::percentSlope(deg) : deg;
  const float a = fabsf(v);
  const char *s = !sign ? "" : v < 0 ? "\xE2\x88\x92" : "+";  // U+2212 minus
  if (saved_.percent)
    snprintf(buf, n, a < 100 ? "%s%.1f%%" : "%s%.0f%%", s, a);
  else
    snprintf(buf, n, "%s%.1f\xC2\xB0", s, a);  // U+00B0 degree
}

void LevelApp::save() { core::store::save("level", &saved_, sizeof saved_); }

// ---------------------------------------------------------------- sensing

void LevelApp::sample() {
  level::Accel a;
  if (!core::imu::read(a)) return;
  const level::Accel &s = filter_.push(a);
  view_ = level::chooseView(view_, s);
  if (hold_) return;
  shown_.view = view_;
  shown_.flat = level::flatAngles(s);
  shown_.edge = level::edgeAngles(s);
  if (saved_.flatZeroed) shown_.flat.x -= saved_.fx, shown_.flat.y -= saved_.fy;
  if (saved_.edgeZeroed) {
    shown_.edge.rotation = level::wrap180(shown_.edge.rotation - saved_.rot);
    shown_.edge.lean -= saved_.lean;
  }
  shown_.ref = level::nearestRef(shown_.edge.rotation, &shown_.ref);
  if (shown_.view == View::Flat)
    beepIfLevel(hypotf(shown_.flat.x, shown_.flat.y));
  else
    beepIfLevel(fmaxf(fabsf(shown_.ref.dev), fabsf(shown_.edge.lean)));
}

// One short beep when the reading becomes level (with a little hysteresis so
// noise at the threshold doesn't chirp).
void LevelApp::beepIfLevel(float dev) {
  const bool isLevel = wasLevel_ ? dev <= level::kLevelDeg * 1.5f : dev <= level::kLevelDeg;
  const uint32_t now = millis();
  if (LEVEL_BEEP && isLevel && !wasLevel_ && now - lastBeepMs_ > 1500) {
    core::sound::play(core::sound::Pattern::Confirm);
    lastBeepMs_ = now;
  }
  wasLevel_ = isLevel;
}

// Redraw only when something visible changed (0.05° steps, view, buttons).
void LevelApp::render() {
  int32_t key[4];
  if (shown_.view == View::Flat) {
    key[0] = 0;
    key[1] = lroundf(shown_.flat.x * 20);
    key[2] = lroundf(shown_.flat.y * 20);
  } else {
    key[0] = 1;
    key[1] = lroundf(shown_.edge.rotation * 20);
    key[2] = lroundf(shown_.edge.lean * 20);
  }
  key[3] = (hold_ ? 1 : 0) | (zeroed() ? 2 : 0) | (saved_.percent ? 4 : 0);
  if (memcmp(key, drawnKey_, sizeof key) == 0) return;
  memcpy(drawnKey_, key, sizeof key);
  lv_obj_invalidate(stage_);
}

// ---------------------------------------------------------------- drawing

void LevelApp::drawHeader(lv_layer_t *layer, float ox, float oy) {
  const bool edge = shown_.view == View::Edge;
  const char *title = !edge ? "SURFACE" : shown_.ref.plumb ? "EDGE \xC2\xB7 PLUMB" : "EDGE \xC2\xB7 LEVEL";
  // Same place as ui::header(): top-left corner at (kEdge, kEdge).
  const int32_t y = int32_t(oy) + ui::kEdge + lv_font_get_line_height(&font_caption) / 2;
  draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, title, int32_t(ox) + ui::kEdge, y, 1,
             LV_TEXT_ALIGN_LEFT);
  const char *tag = hold_ ? "HELD" : zeroed() ? "ZEROED" : nullptr;
  if (tag)
    draw::text(layer, &font_caption, LEVEL_COLOR, LV_OPA_COVER, tag, int32_t(ox) + LCD_WIDTH - ui::kEdge, y, 1,
               LV_TEXT_ALIGN_RIGHT);
}

void LevelApp::drawFlat(lv_layer_t *layer, float ox, float oy) {
  const float cx = ox + LCD_WIDTH / 2.0f, cy = oy + 214, R = 118;
  const level::Flat f = shown_.flat;
  const float mag = hypotf(f.x, f.y);
  const uint32_t col = stateColor(mag);

  draw::circle(layer, cx, cy, R, kVial, LV_OPA_COVER);
  draw::ring(layer, cx, cy, R, 2, kVialRim, LV_OPA_COVER);
  const float rings[3][2] = {{1, 0.32f}, {3, 0.18f}, {6, 0.11f}};
  for (const auto &r : rings)
    draw::ring(layer, cx, cy, level::bubbleRadius(r[0], R), 1, LEVEL_COLOR, lv_opa_t(r[1] * 255));
  draw::line(layer, cx - R, cy, cx + R, cy, ui::col::text, 1, 20);
  draw::line(layer, cx, cy - R, cx, cy + R, ui::col::text, 1, 20);
  draw::ring(layer, cx, cy, 30, 2, level::stateOf(mag) == State::Level ? ui::col::success : kRings, LV_OPA_COVER);

  // The bubble floats to the high side: opposite the low side (x, y).
  const float d = level::bubbleRadius(mag, R - 30), ang = atan2f(-f.y, -f.x);
  const float bx = cx + (mag > 1e-3f ? cosf(ang) * d : 0), by = cy + (mag > 1e-3f ? sinf(ang) * d : 0);
  draw::circle(layer, bx, by, 25, col, 230);
  draw::circle(layer, bx - 8, by - 9, 7, 0xFFFFFF, 150);  // highlight

  char buf[16];
  draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, "X", int32_t(ox + 24), int32_t(oy + 64), 1,
             LV_TEXT_ALIGN_LEFT);
  fmt(buf, sizeof buf, f.x, true);
  draw::text(layer, &font_body_medium, stateColor(f.x), LV_OPA_COVER, buf, int32_t(ox + 42), int32_t(oy + 64), 0,
             LV_TEXT_ALIGN_LEFT);
  draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, "Y", int32_t(ox + LCD_WIDTH - 24), int32_t(oy + 64),
             1, LV_TEXT_ALIGN_RIGHT);
  fmt(buf, sizeof buf, f.y, true);
  draw::text(layer, &font_body_medium, stateColor(f.y), LV_OPA_COVER, buf, int32_t(ox + LCD_WIDTH - 42),
             int32_t(oy + 64), 0, LV_TEXT_ALIGN_RIGHT);
  fmt(buf, sizeof buf, mag, false);
  draw::text(layer, &font_digits_32, col, LV_OPA_COVER, buf, int32_t(cx), int32_t(oy + 356));
}

// Attitude horizon: rotation tilts the horizon, lean slides it along its
// normal (5 px per degree), like an aircraft's artificial horizon.
void LevelApp::drawEdge(lv_layer_t *layer, float ox, float oy) {
  const float cx = ox + LCD_WIDTH / 2.0f, cy = oy + 214, R = 132, ppd = 5;
  const level::Ref ref = shown_.ref;
  const float lean = shown_.edge.lean, phi = -ref.dev * kD2R;
  const float dx = cosf(phi), dy = sinf(phi);  // along the horizon
  const float nx = -dy, ny = dx;               // toward the ground
  const float off = lean * ppd;

  // Sky disc, then the ground as one big triangle over the half-plane below
  // the horizon (a single triangle has no seams), then black outside the disc.
  draw::circle(layer, cx, cy, R, kSky, LV_OPA_COVER);
  const float hx = cx + nx * off, hy = cy + ny * off, far = 4 * R;
  const float ground[3][2] = {
      {hx - dx * far, hy - dy * far}, {hx + dx * far, hy + dy * far}, {hx + nx * far, hy + ny * far}};
  draw::polygon(layer, ground, 3, kGround, LV_OPA_COVER);
  draw::ring(layer, cx, cy, R + 120, 240, ui::col::bg, LV_OPA_COVER);

  // Horizon line and pitch ladder, kept inside the disc.
  auto inside = [&](float x, float y, float margin) { return hypotf(x - cx, y - cy) <= R - margin; };
  auto chord = [&](float o, float half, uint32_t color, int32_t w, lv_opa_t opa) {
    const float mx = cx + nx * o, my = cy + ny * o;
    const float x1 = mx - dx * half, y1 = my - dy * half, x2 = mx + dx * half, y2 = my + dy * half;
    if (inside(x1, y1, 3) && inside(x2, y2, 3)) draw::line(layer, x1, y1, x2, y2, color, w, opa);
  };
  const float hc = sqrtf(fmaxf(0, R * R - off * off)) - 4;  // horizon spans the whole chord
  if (fabsf(off) < R) chord(off, hc, LEVEL_COLOR, 3, LV_OPA_COVER);
  char buf[16];
  for (int p = -60; p <= 60; p += 5) {
    if (!p) continue;
    const float o = (lean - p) * ppd, half = p % 10 ? 22 : 46;
    chord(o, half, ui::col::text, p % 10 ? 1 : 2, 140);
    if (p % 10) continue;
    snprintf(buf, sizeof buf, "%d", abs(p));
    for (int s = -1; s <= 1; s += 2) {
      const float lx = cx + nx * o + dx * s * (half + 16), ly = cy + ny * o + dy * s * (half + 16);
      if (inside(lx, ly, 12))
        draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, buf, int32_t(lx), int32_t(ly));
    }
  }
  draw::ring(layer, cx, cy, R, 2, ui::col::outline, LV_OPA_COVER);

  // Roll scale above the disc and its pointer.
  static const int kTicks[] = {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60};
  for (int a : kTicks) {
    const float t = (a - 90) * kD2R, len = a % 30 ? 7 : 12;
    draw::line(layer, cx + cosf(t) * (R + 5), cy + sinf(t) * (R + 5), cx + cosf(t) * (R + 5 + len),
               cy + sinf(t) * (R + 5 + len), a ? ui::col::textDim : ui::col::text, 2, LV_OPA_COVER);
  }
  const float t = phi - 90 * kD2R, half = 0.065f;  // pointer: tip on the rim, 8 px wide base
  const float tri[3][2] = {{cx + cosf(t) * (R - 2), cy + sinf(t) * (R - 2)},
                           {cx + cosf(t - half) * (R - 16), cy + sinf(t - half) * (R - 16)},
                           {cx + cosf(t + half) * (R - 16), cy + sinf(t + half) * (R - 16)}};
  draw::polygon(layer, tri, 3, stateColor(ref.dev), LV_OPA_COVER);

  // Fixed aircraft symbol: green only when both rotation and lean are level.
  const uint32_t col = stateColor(fmaxf(fabsf(ref.dev), fabsf(lean)));
  draw::line(layer, cx - 62, cy, cx - 22, cy, col, 4, LV_OPA_COVER);
  draw::line(layer, cx - 22, cy, cx - 10, cy + 10, col, 4, LV_OPA_COVER);
  draw::line(layer, cx + 62, cy, cx + 22, cy, col, 4, LV_OPA_COVER);
  draw::line(layer, cx + 22, cy, cx + 10, cy + 10, col, 4, LV_OPA_COVER);
  draw::circle(layer, cx, cy, 4, col, LV_OPA_COVER);

  // Readouts: deviation from level / plumb, and the lean.
  fmt(buf, sizeof buf, ref.dev, false);
  draw::text(layer, &font_digits_24, stateColor(ref.dev), LV_OPA_COVER, buf, int32_t(ox + 24), int32_t(oy + 338), 0,
             LV_TEXT_ALIGN_LEFT);
  draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, ref.plumb ? "PLUMB" : "LEVEL", int32_t(ox + 24),
             int32_t(oy + 362), 1, LV_TEXT_ALIGN_LEFT);
  fmt(buf, sizeof buf, lean, false);
  draw::text(layer, &font_digits_24, stateColor(lean), LV_OPA_COVER, buf, int32_t(ox + LCD_WIDTH - 24),
             int32_t(oy + 338), 0, LV_TEXT_ALIGN_RIGHT);
  draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, lean >= 0 ? "LEAN UP" : "LEAN DOWN",
             int32_t(ox + LCD_WIDTH - 24), int32_t(oy + 362), 1, LV_TEXT_ALIGN_RIGHT);
  snprintf(buf, sizeof buf, "rotation %.1f\xC2\xB0", fabsf(level::wrap180(shown_.edge.rotation)));
  draw::text(layer, &font_caption, ui::col::textOff, LV_OPA_COVER, buf, int32_t(ox + 24), int32_t(oy + 52), 0,
             LV_TEXT_ALIGN_LEFT);
}

void LevelApp::draw(lv_layer_t *layer) {
  lv_area_t a;
  lv_obj_get_coords(stage_, &a);  // follows pixel shift
  if (!core::imu::present())
    draw::text(layer, &font_body, ui::col::textDim, LV_OPA_COVER, "Motion sensor not found", a.x1 + LCD_WIDTH / 2,
               a.y1 + 200);
  else if (shown_.view == View::Flat)
    drawFlat(layer, float(a.x1), float(a.y1));
  else
    drawEdge(layer, float(a.x1), float(a.y1));
  drawHeader(layer, float(a.x1), float(a.y1));  // last: the horizon masks its surroundings
}

// ---------------------------------------------------------------- widgets

void LevelApp::syncButtons() {
  if (!stage_) return;
  const bool z = zeroed();
  ui::setText(lv_obj_get_child(zeroBtn_, 0), z ? "Reset" : "Zero");
  auto toggleColors = [](lv_obj_t *b, bool on) {
    lv_obj_set_style_bg_color(b, ui::c(on ? LEVEL_COLOR : ui::col::surface), 0);
    lv_obj_set_style_text_color(b, ui::c(on ? ui::mixBlack(LEVEL_COLOR, 15) : ui::col::text), 0);
  };
  toggleColors(zeroBtn_, z);
  toggleColors(holdBtn_, hold_);
  ui::setSegment(units_, saved_.percent, ui::c(ui::col::surfaceHi), ui::c(ui::col::text));
}

void LevelApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  if (!loaded_) {
    Saved s;
    if (core::store::load("level", &s, sizeof s)) saved_ = s;
    loaded_ = true;
  }
  stage_ = box(root);
  lv_obj_set_size(stage_, LCD_WIDTH, kStageH);
  lv_obj_add_event_cb(stage_, onStageDraw, LV_EVENT_DRAW_MAIN, this);

  auto toggleButton = [&](const char *txt, int32_t x, lv_event_cb_t cb) {
    lv_obj_t *b = textButton(root, txt, col::text, kBtnSmall);
    lv_obj_set_width(b, 96);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, x, -kBarBottom);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
    return b;
  };
  zeroBtn_ = toggleButton("Zero", kEdge, onZero);
  holdBtn_ = toggleButton("Hold", kEdge + 96 + 8, onHold);

  static const char *const kUnits[2] = {"\xC2\xB0", "%"};
  units_ = segmented(root, kUnits, 2, &font_body_medium, 46, onUnit, this);
  lv_obj_align(units_, LV_ALIGN_BOTTOM_RIGHT, -kEdge, -kBarBottom);

  core::imu::enable(true);
  filter_.reset();
  hold_ = false;
  wasLevel_ = false;
  memset(drawnKey_, 0xFF, sizeof drawnKey_);
  nextSampleMs_ = nextFrameMs_ = millis();
  sample();
  syncButtons();
}

void LevelApp::onDestroy() {
  core::imu::enable(false);
  stage_ = zeroBtn_ = holdBtn_ = units_ = nullptr;
}

void LevelApp::onTick(uint32_t nowMs) {
  if (!stage_) return;
  if (core::reached(nowMs, nextSampleMs_)) {
    nextSampleMs_ = nowMs + LEVEL_SAMPLE_MS;
    sample();
  }
  if (core::reached(nowMs, nextFrameMs_)) {
    nextFrameMs_ = nowMs + kFrameMs;
    render();
  }
}

// ---------------------------------------------------------------- events

void LevelApp::onStageDraw(lv_event_t *e) {
  static_cast<LevelApp *>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));
}

// Zero measures relative to the current surface, per view; tap again to reset.
void LevelApp::onZero(lv_event_t *e) {
  auto *self = static_cast<LevelApp *>(lv_event_get_user_data(e));
  Saved &s = self->saved_;
  const level::Accel &g = self->filter_.value();
  if (self->shown_.view == View::Flat) {
    if (s.flatZeroed) {
      s.flatZeroed = 0;
    } else {
      const level::Flat f = level::flatAngles(g);
      s.fx = f.x, s.fy = f.y, s.flatZeroed = 1;
    }
  } else {
    if (s.edgeZeroed) {
      s.edgeZeroed = 0;
    } else {
      const level::Edge ed = level::edgeAngles(g);
      s.rot = ed.rotation, s.lean = ed.lean, s.edgeZeroed = 1;
    }
  }
  self->save();
  self->hold_ = false;
  self->sample();
  self->syncButtons();
}

void LevelApp::onHold(lv_event_t *e) {
  auto *self = static_cast<LevelApp *>(lv_event_get_user_data(e));
  self->hold_ = !self->hold_;
  if (!self->hold_) self->sample();
  self->syncButtons();
}

void LevelApp::onUnit(lv_event_t *e) {
  auto *self = static_cast<LevelApp *>(lv_event_get_user_data(e));
  self->saved_.percent = uint8_t(ui::eventIndex(e));
  self->save();
  self->syncButtons();
}

}  // namespace apps
