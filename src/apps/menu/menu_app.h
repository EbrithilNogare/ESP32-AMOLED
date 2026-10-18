#pragma once
#include "apps/app.h"
#include "quick_settings.h"

namespace apps {

// The launcher: a status bar plus round app icons in pairs, scrolling
// vertically with row snapping (docs/UI_DESIGN.md §5, mockup "App menu").
// Pulling the status bar down opens the quick settings.
class MenuApp : public App {
 public:
  explicit MenuApp(App *wifiSettings) : wifiSettings_(wifiSettings) {}
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onTick(uint32_t nowMs) override;
  bool onBack() override;  // Home/Back in the menu: close the shade, else scroll to top
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

  QuickSettings &quickSettings() { return quick_; }

 private:
  void updateStatus();
  static void onScroll(lv_event_t *e);
  static void onAppClicked(lv_event_t *e);

  App *wifiSettings_;
  QuickSettings quick_;
  lv_obj_t *time_ = nullptr;
  lv_obj_t *wifi_ = nullptr;
  lv_obj_t *batIcon_ = nullptr;
  lv_obj_t *batText_ = nullptr;
  lv_obj_t *list_ = nullptr;
  uint32_t nextStatusMs_ = 0;
  int32_t scrollY_ = 0;  // restored after a sleep
};

}  // namespace apps
