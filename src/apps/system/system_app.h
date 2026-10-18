#pragma once
#include <touch_cal.h>

#include "apps/app.h"

namespace apps {

// System (docs/apps/system.md): device info, the wireless update screen and
// touch calibration (with a reset to the factory calibration).
class SystemApp : public App {
 public:
  static constexpr int kRows = 9;  // info rows
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;
  bool blocksNavigation() const override;
  power::Policy powerPolicy() const override;

 private:
  void showInfo();
  void showUpdate();
  void showTouch();
  void placeCross();
  void finishTouch(bool save);
  static void onTouchClicked(lv_event_t *e);
  static void onCatcher(lv_event_t *e);
  static void onTouchButton(lv_event_t *e);
  static void onFactory(lv_event_t *e);
  void updateInfo();
  void updateOta(uint32_t nowMs);
  static void onUpdateClicked(lv_event_t *e);

  lv_obj_t *root_ = nullptr;
  bool updateView_ = false;
  // info view
  lv_obj_t *values_[kRows] = {};
  uint32_t nextInfoMs_ = 0;
  // update view
  lv_obj_t *arc_ = nullptr, *icon_ = nullptr, *pct_ = nullptr, *status_ = nullptr, *line1_ = nullptr;
  uint32_t doneAtMs_ = 0;
  uint32_t otaColor_ = 0;  // style setters redraw even when unchanged
  bool restarting_ = false;
  bool openUpdate_ = false;  // deferred: the click handler's button gets deleted
  // touch calibration: kCalTargets crosses, then a check with a dot
  enum class Pending : uint8_t { None, Touch, Redo, Save, Factory };
  static constexpr int kCalTargets = 5;
  bool touchView_ = false;
  Pending pending_ = Pending::None;  // deferred like openUpdate_
  int calStep_ = 0;                  // target being tapped; kCalTargets: checking
  touchcal::Sample samples_[kCalTargets] = {};
  touchcal::Cal cal_;
  lv_obj_t *cross_ = nullptr, *dot_ = nullptr, *hint_ = nullptr, *step_ = nullptr, *buttons_ = nullptr;
  lv_obj_t *factory_ = nullptr;
};

}  // namespace apps
