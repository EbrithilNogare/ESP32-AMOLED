#pragma once
#include <pomodoro_core.h>

#include "apps/app.h"
#include "config.h"

namespace apps {

// Pomodoro (docs/apps/pomodoro.md): the timer logic is lib/PomodoroCore; this
// class draws the full view and the minimal view, beeps at phase ends and
// keeps running while other apps are open, and through a sleep: a phase end
// wakes the device.
class PomodoroApp : public App {
 public:
  PomodoroApp();
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void service(uint32_t nowMs) override;

  power::Policy powerPolicy() const override;
  power::Policy backgroundPolicy() const override;

  void enterMinimal(lv_obj_t *layer) override;
  void exitMinimal() override;
  void minimalTick(uint32_t nowMs) override;
  uint32_t minimalRefreshMs(uint32_t nowMs) const override;
  sleepplan::Need sleepNeed(uint64_t monoNow) const override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

 private:
  void render(uint32_t nowMs);
  void renderMinimal(uint32_t nowMs);
  void flash();
  static void onToggle(lv_event_t *e);
  static void onReset(lv_event_t *e);
  static void onRing(lv_event_t *e);

  static constexpr int kMinDots = 40;  // minimal view: dotted ring of the remaining time

  pomo::Pomodoro timer_;
  uint32_t confirmResetUntil_ = 0;

  lv_obj_t *root_ = nullptr;
  lv_obj_t *phase_ = nullptr, *dots_[POMO_SET_SIZE] = {}, *arc_ = nullptr, *digits_ = nullptr, *sub_ = nullptr;
  lv_obj_t *toggle_ = nullptr, *toast_ = nullptr;
  uint32_t lastRenderSec_ = UINT32_MAX;

  lv_obj_t *minLayer_ = nullptr, *minDigits_ = nullptr;
  lv_obj_t *minDots_[kMinDots] = {};
};

}  // namespace apps
