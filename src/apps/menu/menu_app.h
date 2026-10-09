#pragma once
#include "apps/app.h"

namespace apps {

// The launcher: a status bar plus round app icons in pairs, scrolling
// vertically with row snapping (docs/UI_DESIGN.md §5, mockup "App menu").
class MenuApp : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;  // Home/Back in the menu: scroll to top

 private:
  void updateStatus();
  static void onScroll(lv_event_t *e);
  static void onAppClicked(lv_event_t *e);

  lv_obj_t *time_ = nullptr;
  lv_obj_t *wifi_ = nullptr;
  lv_obj_t *batIcon_ = nullptr;
  lv_obj_t *batText_ = nullptr;
  lv_obj_t *list_ = nullptr;
  uint32_t nextStatusMs_ = 0;
};

}  // namespace apps
