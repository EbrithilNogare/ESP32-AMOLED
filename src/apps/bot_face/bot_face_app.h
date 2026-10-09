#pragma once
#include <bot_face.h>

#include "apps/app.h"

namespace apps {

// The approved mockup face (docs/apps/bot-face.md). All behaviour lives in
// lib/BotFace; this class only maps its geometry onto LVGL objects. LVGL
// redraws just the moved shapes, so a frame touches the face area only.
class BotFaceApp : public App {
 public:
  explicit BotFaceApp(bool (*isDimmed)());
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;

 private:
  static void onPressed(lv_event_t *e);
  void draw(uint32_t nowMs);

  bool (*isDimmed_)();
  bot::Face face_;
  lv_obj_t *eyeL_ = nullptr, *eyeR_ = nullptr, *mouth_ = nullptr, *mouthO_ = nullptr;
  lv_point_precise_t mouthPts_[13];
  uint32_t nextFrameMs_ = 0;
};

}  // namespace apps
