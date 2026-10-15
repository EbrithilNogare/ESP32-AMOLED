#pragma once
#include <level_core.h>

#include "apps/app.h"

namespace apps {

// Level (docs/apps/level.md): a bullseye bubble vial while the board lies
// flat, an attitude horizon (rotation + lean) while it stands on an edge.
// The maths is lib/LevelCore; this class samples the IMU, keeps Zero / Hold /
// units, and draws in the stage object's draw event.
class LevelApp : public App {
 public:
  LevelApp();
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  uint32_t idleDimMs() const override;

  level::View view() const { return shown_.view; }
  bool held() const { return hold_; }
  bool zeroed() const;

 private:
  struct Reading {
    level::View view;
    level::Flat flat;  // after Zero
    level::Edge edge;  // after Zero
    level::Ref ref;    // nearest level / plumb reference for edge.rotation
  };
  struct Saved {
    float fx, fy, rot, lean;
    uint8_t flatZeroed, edgeZeroed, percent;
  };

  void sample();
  void render();  // invalidates the stage when what's on screen changed
  void syncButtons();
  void save();
  void draw(lv_layer_t *layer);
  void drawFlat(lv_layer_t *layer, float ox, float oy);
  void drawEdge(lv_layer_t *layer, float ox, float oy);
  void drawHeader(lv_layer_t *layer, float ox, float oy);
  void fmt(char *buf, size_t n, float deg, bool sign) const;
  void beepIfLevel(float dev);

  static void onStageDraw(lv_event_t *e);
  static void onZero(lv_event_t *e);
  static void onHold(lv_event_t *e);
  static void onUnit(lv_event_t *e);

  level::Smoother filter_;
  level::View view_ = level::View::Flat;
  Reading shown_{};
  bool hold_ = false;
  bool loaded_ = false;
  Saved saved_{};
  uint32_t nextSampleMs_ = 0, nextFrameMs_ = 0, lastBeepMs_ = 0;
  bool wasLevel_ = false;
  int32_t drawnKey_[4] = {};

  lv_obj_t *stage_ = nullptr, *zeroBtn_ = nullptr, *holdBtn_ = nullptr, *units_ = nullptr;
};

}  // namespace apps
