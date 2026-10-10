#include "noise_app.h"

#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <stdlib.h>

#include "board.h"
#include "config.h"
#include "core/power.h"
#include "core/settings.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/draw.h"
#include "ui/theme.h"

namespace apps {

using noise::Kind;

static const AppInfo kInfo = {"noise", "Noise", ICON_RAINY, NOISE_COLOR};
static constexpr uint32_t kChromeMs = 4000;  // labels fade out this long after the last touch
static constexpr int32_t kSwipePx = 50;
static constexpr int32_t kTapSlop = 12;
static constexpr uint32_t kRain = 0x9DB7FF, kSnow = 0xE8ECF2, kBlue = 0x3D6BFF;

const AppInfo &NoiseApp::info() const { return kInfo; }

bool NoiseApp::playing() const { return core::sound::noisePlaying(); }

power::Policy NoiseApp::powerPolicy() const { return playing() ? power::Policy::NoSleep : power::Policy::Normal; }
power::Policy NoiseApp::backgroundPolicy() const { return powerPolicy(); }

void NoiseApp::onSleep() { core::sound::stopNoise(); }

float NoiseApp::rnd() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ >> 8) * (1.0f / 16777216.0f);
}

// ---------------------------------------------------------------- animation

// Rain: streaks in three depths (far ones short, dim and slow). White noise:
// soft snow drifting down. Blue noise: a jittered grid of twinkling points
// rising slowly. Everything keeps moving, so no pixel stays lit for long.
void NoiseApp::seed() {
  for (int i = 0; i < kParticles; i++) {
    Particle &p = parts_[i];
    p.depth = uint8_t(i % 3);
    p.x = rnd() * LCD_WIDTH;
    p.y = rnd() * LCD_HEIGHT;
    p.phase = rnd() * 6.283f;
    switch (kind_) {
      case Kind::Rain:
        p.v = 420 + 260 * p.depth + rnd() * 120;  // px/s
        p.size = 10 + 9 * p.depth + rnd() * 8;    // streak length
        break;
      case Kind::White:
        p.v = 14 + 12 * p.depth + rnd() * 10;
        p.size = 1.2f + 0.8f * p.depth + rnd() * 0.8f;
        break;
      case Kind::Blue: {  // 8 x 9 jittered grid: even coverage, no clumps
        const int col = i % 8, row = i / 8;
        p.x = (col + 0.2f + 0.6f * rnd()) * LCD_WIDTH / 8;
        p.y = (row + 0.2f + 0.6f * rnd()) * LCD_HEIGHT / 9;
        p.v = 6 + 4 * rnd();
        p.size = 1.4f + rnd();
        break;
      }
    }
  }
  for (Splash &s : splashes_) s.age = -1;
}

void NoiseApp::step(float dt) {
  t_ += dt;
  for (Particle &p : parts_) {
    switch (kind_) {
      case Kind::Rain:
        p.y += p.v * dt;
        p.x -= p.v * dt * 0.12f;  // a little wind
        if (p.y - p.size > LCD_HEIGHT) {
          if (p.depth == 2) {  // near drops splash
            for (Splash &s : splashes_)
              if (s.age < 0) {
                s = {p.x, LCD_HEIGHT - 40 - rnd() * 140, 0};
                break;
              }
          }
          p.y = -rnd() * 60;
          p.x = rnd() * (LCD_WIDTH + 60);
        }
        if (p.x < -20) p.x += LCD_WIDTH + 40;
        break;
      case Kind::White:
        p.y += p.v * dt;
        p.x += sinf(t_ * 0.7f + p.phase) * 8 * dt;
        if (p.y > LCD_HEIGHT + 4) {
          p.y = -4;
          p.x = rnd() * LCD_WIDTH;
        }
        break;
      case Kind::Blue:
        p.y -= p.v * dt;
        if (p.y < -4) p.y += LCD_HEIGHT + 8;
        break;
    }
  }
  for (Splash &s : splashes_)
    if (s.age >= 0 && (s.age += dt) > 0.45f) s.age = -1;
}

void NoiseApp::draw(lv_layer_t *layer) {
  lv_area_t a;
  lv_obj_get_coords(stage_, &a);  // follows the pixel shift
  const float ox = float(a.x1), oy = float(a.y1);
  const float k = playing() ? 1.0f : 0.45f;  // paused: dimmer, and still
  static const lv_opa_t kDepthOpa[3] = {70, 130, 210};
  for (const Particle &p : parts_) {
    const lv_opa_t opa = lv_opa_t(kDepthOpa[p.depth] * k);
    switch (kind_) {
      case Kind::Rain:
        ui::draw::line(layer, ox + p.x, oy + p.y, ox + p.x + p.size * 0.12f, oy + p.y - p.size, kRain,
                       p.depth == 2 ? 2 : 1, opa);
        break;
      case Kind::White: ui::draw::circle(layer, ox + p.x, oy + p.y, p.size, kSnow, opa); break;
      case Kind::Blue: {
        const float tw = 0.5f + 0.5f * sinf(t_ * 2.3f + p.phase);  // twinkle
        ui::draw::circle(layer, ox + p.x, oy + p.y, p.size, kBlue, lv_opa_t((60 + 170 * tw) * k));
        break;
      }
    }
  }
  // Splash: three droplets thrown up and out from where a drop landed.
  for (const Splash &s : splashes_) {
    if (s.age < 0) continue;
    const float t = s.age, f = t / 0.45f;
    for (int d = -1; d <= 1; d++) {
      const float x = s.x + d * 34 * t, y = s.y - (d ? 70 : 95) * t + 300 * t * t;
      ui::draw::circle(layer, ox + x, oy + y, 1.3f, kRain, lv_opa_t(190 * (1 - f) * k));
    }
  }
}

// ---------------------------------------------------------------- view

void NoiseApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  if (!loaded_) {
    uint8_t k;
    if (core::store::load("noise", &k, 1) && k < noise::kKinds) kind_ = Kind(k);
    rng_ = esp_random() | 1;
    loaded_ = true;
  }
  stage_ = box(root);
  lv_obj_set_size(stage_, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_clickable(stage_, true);
  lv_obj_add_event_cb(stage_, onDraw, LV_EVENT_DRAW_MAIN, this);
  lv_obj_add_event_cb(stage_, onTouch, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(stage_, onTouch, LV_EVENT_RELEASED, this);

  name_ = label(root, &font_title, col::text, "");
  lv_obj_align(name_, LV_ALIGN_TOP_MID, 0, kEdge);
  state_ = label(root, &font_caption, col::textDim, "");
  lv_obj_align(state_, LV_ALIGN_TOP_MID, 0, kEdge + 40);

  // Paused: a play button in the middle (the whole screen is the button).
  play_ = box(root);
  lv_obj_set_size(play_, 112, 112);
  lv_obj_align(play_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(play_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(play_, c(col::surface), 0);
  lv_obj_set_style_bg_opa(play_, LV_OPA_80, 0);
  lv_obj_center(label(play_, &font_icon_64, col::text, ICON_PLAY_ARROW));

  muted_ = label(root, &font_caption, col::warning, "Muted: unmute in quick settings");
  lv_obj_align(muted_, LV_ALIGN_BOTTOM_MID, 0, -(kBarBottom + 34));

  for (int i = 0; i < noise::kKinds; i++) {  // which sound, of three
    dots_[i] = box(root);
    lv_obj_set_size(dots_[i], 8, 8);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, 0);
    lv_obj_align(dots_[i], LV_ALIGN_BOTTOM_MID, (i - 1) * 18, -kBarBottom - 4);
  }

  seed();
  step(0);
  nextFrameMs_ = lastFrameMs_ = millis();
  showChrome(millis());
}

void NoiseApp::onDestroy() {
  stage_ = name_ = state_ = play_ = muted_ = nullptr;
  for (auto &d : dots_) d = nullptr;
}

void NoiseApp::render() {
  const bool on = shownPlaying_ = playing();
  ui::setText(name_, noise::name(kind_));
  ui::setText(state_, on ? "playing" : "tap to play  ·  swipe to change");
  lv_obj_set_hidden(play_, on);
  lv_obj_set_hidden(muted_, !core::settings::muted());
  for (int i = 0; i < noise::kKinds; i++)
    lv_obj_set_style_bg_color(dots_[i], ui::c(i == int(kind_) ? NOISE_COLOR : ui::col::outline), 0);
  // The labels go away while it plays untouched (burn-in, and calm); the
  // mute warning stays, it explains the silence.
  lv_obj_set_hidden(name_, !chrome_);
  lv_obj_set_hidden(state_, !chrome_);
  for (auto *d : dots_) lv_obj_set_hidden(d, !chrome_);
}

void NoiseApp::showChrome(uint32_t nowMs) {
  chrome_ = true;
  chromeUntil_ = nowMs + kChromeMs;
  render();
}

void NoiseApp::onTick(uint32_t nowMs) {
  if (!stage_) return;
  if (playing() != shownPlaying_) showChrome(nowMs);  // e.g. the codec failed to start
  if (chrome_ && playing() && core::reached(nowMs, chromeUntil_)) {
    chrome_ = false;
    render();
  }
  if (!playing() || !core::reached(nowMs, nextFrameMs_)) return;
  const ::power::Stage st = core::power::stage();
  if (st == ::power::Stage::Black) return;  // panel off: nothing to animate
  nextFrameMs_ = nowMs + 1000 / (st == ::power::Stage::Dimmed ? NOISE_FPS_DIM : NOISE_FPS);
  const float dt = fminf(float(nowMs - lastFrameMs_) / 1000.0f, 0.15f);
  lastFrameMs_ = nowMs;
  step(dt);
  lv_obj_invalidate(stage_);
}

void NoiseApp::setKind(Kind k) {
  if (k == kind_) return;
  kind_ = k;
  const uint8_t v = uint8_t(k);
  core::store::save("noise", &v, 1);
  seed();
  step(0);
  lv_obj_invalidate(stage_);
}

// Tap: play / pause. Swipe left / right: the next / previous sound, playing.
void NoiseApp::onTouch(lv_event_t *e) {
  auto *self = static_cast<NoiseApp *>(lv_event_get_user_data(e));
  lv_point_t p{0, 0};
  if (lv_indev_t *in = lv_indev_active()) lv_indev_get_point(in, &p);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    self->press_ = p;
    return;
  }
  const int32_t dx = p.x - self->press_.x, dy = p.y - self->press_.y;
  if (abs(dx) > kSwipePx && abs(dx) > abs(dy)) {
    self->setKind(noise::next(self->kind_, dx < 0 ? 1 : -1));
    core::sound::startNoise(self->kind_);
  } else if (abs(dx) <= kTapSlop && abs(dy) <= kTapSlop) {
    if (self->playing())
      core::sound::stopNoise();
    else
      core::sound::startNoise(self->kind_);
  } else {
    return;  // a vague drag: ignore
  }
  self->lastFrameMs_ = millis();  // no jump after a pause
  self->showChrome(millis());
  lv_obj_invalidate(self->stage_);
}

void NoiseApp::onDraw(lv_event_t *e) {
  static_cast<NoiseApp *>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));
}

}  // namespace apps
