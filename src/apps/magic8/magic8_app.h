#pragma once
#include <eight_ball.h>

#include "apps/app.h"

namespace apps {

// Magic 8-ball (docs/apps/magic8.md): shake or tap, read the answer on a red
// paint swipe in brush lettering. The answers and the shake detector are
// lib/EightBall; this class draws the ball into one stage object (its
// LV_EVENT_DRAW_MAIN) and animates it frame by frame. The IMU runs only
// while it's on screen.
class Magic8App : public App {
 public:
  Magic8App();
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void onScreen(bool on) override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

  enum class Phase : uint8_t { Idle, Shaking, Revealing, Answer };
  Phase phase() const { return phase_; }
  int answer() const { return answer_; }

  void ask(uint32_t nowMs);  // a tap on the ball: shake briefly, then answer

 private:
  struct Parked {
    int8_t answer;
    uint8_t showing;
  };

  void setPhase(Phase p, uint32_t nowMs);
  void sample(uint32_t nowMs);
  void draw(lv_layer_t *layer);
  static void onStageDraw(lv_event_t *e);
  static void onStageClick(lv_event_t *e);

  eightball::Shake shake_;
  Phase phase_ = Phase::Idle;
  int answer_ = -1;          // the one shown (or coming), -1 none yet
  uint32_t phaseMs_ = 0;     // when the phase began
  uint32_t settleAtMs_ = 0;  // a tap's shake ends here
  bool tapped_ = false;      // the shake came from a tap, not the IMU
  uint32_t nextSampleMs_ = 0, nextFrameMs_ = 0;
  lv_obj_t *stage_ = nullptr;
};

}  // namespace apps
