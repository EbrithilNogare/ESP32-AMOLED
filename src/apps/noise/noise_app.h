#pragma once
#include <noise_gen.h>

#include "apps/app.h"

namespace apps {

// Noise (docs/apps/noise.md): rain with a rain animation; swipe left or
// right to cycle through white and blue noise. Tap to play / pause. The sound
// is lib/NoiseGen streamed by core::sound; it keeps playing in the menu and
// other apps. Long sessions: the labels fade out, the animation never stops
// moving, and the screen still dims and goes black (policy NoSleep).
class NoiseApp : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  power::Policy powerPolicy() const override;
  power::Policy backgroundPolicy() const override;
  void onSleep() override;

  noise::Kind kind() const { return kind_; }
  bool playing() const;
  bool chromeShown() const { return chrome_; }

 private:
  struct Particle {
    float x, y, v, size, phase;
    uint8_t depth;  // 0 far .. 2 near
  };
  struct Splash {
    float x, y, age;  // age in s; < 0: unused
  };
  static constexpr int kParticles = 72;
  static constexpr int kSplashes = 6;

  void seed();
  void step(float dt);
  void draw(lv_layer_t *layer);
  void render();  // labels
  void showChrome(uint32_t nowMs);
  void setKind(noise::Kind k);
  float rnd();  // 0..1

  static void onTouch(lv_event_t *e);
  static void onDraw(lv_event_t *e);

  noise::Kind kind_ = noise::Kind::Rain;
  bool loaded_ = false;
  bool chrome_ = true;
  bool shownPlaying_ = false;  // what render() last showed
  uint32_t rng_ = 1;
  uint32_t chromeUntil_ = 0, nextFrameMs_ = 0, lastFrameMs_ = 0;
  float t_ = 0;  // animation time, s
  lv_point_t press_{0, 0};
  Particle parts_[kParticles] = {};
  Splash splashes_[kSplashes] = {};

  lv_obj_t *stage_ = nullptr, *name_ = nullptr, *state_ = nullptr, *dots_[noise::kKinds] = {};
  lv_obj_t *play_ = nullptr, *muted_ = nullptr;
};

}  // namespace apps
