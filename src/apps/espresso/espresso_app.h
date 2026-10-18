#pragma once
#include <shot_timer.h>

#include "apps/app.h"

namespace apps {

// Espresso shot timer (docs/apps/espresso-timer.md). Timing and history are
// lib/ShotTimer; this class draws them. Tap anywhere: start / stop / next.
class EspressoApp : public App {
 public:
  EspressoApp();
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;
  power::Policy powerPolicy() const override;
  uint32_t idleDimMs() const override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

  const shot::ShotTimer &timer() const { return timer_; }

 private:
  void render(uint32_t nowMs);
  void renderHistory();
  void loadHistory();
  void saveHistory();
  static void onTap(lv_event_t *e);
  static void onLongPress(lv_event_t *e);

  shot::ShotTimer timer_;
  lv_obj_t *digits_ = nullptr, *fill_ = nullptr, *msg_ = nullptr, *hist_ = nullptr;
  uint32_t nextFrameMs_ = 0;
};

}  // namespace apps
