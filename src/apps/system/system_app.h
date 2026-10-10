#pragma once
#include "apps/app.h"

namespace apps {

// System (docs/apps/system.md): device info and the wireless update screen.
class SystemApp : public App {
 public:
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
  void updateInfo();
  void updateOta(uint32_t nowMs);
  static void onUpdateClicked(lv_event_t *e);

  lv_obj_t *root_ = nullptr;
  bool updateView_ = false;
  // info view
  lv_obj_t *values_[8] = {};
  uint32_t nextInfoMs_ = 0;
  // update view
  lv_obj_t *arc_ = nullptr, *icon_ = nullptr, *pct_ = nullptr, *status_ = nullptr, *line1_ = nullptr;
  uint32_t doneAtMs_ = 0;
  uint32_t otaColor_ = 0;  // style setters redraw even when unchanged
  bool restarting_ = false;
  bool openUpdate_ = false;  // deferred: the click handler's button gets deleted
};

}  // namespace apps
