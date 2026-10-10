#pragma once
#include <lvgl.h>

#include "apps/app.h"

namespace apps {

// The quick settings shade (docs/apps/quick-settings.md): pulled down from the
// menu's status bar like on Android. Two rows: Wi-Fi (opens the Wi-Fi
// screen) and a brightness bar that sets the brightness live while touched
// and saves it on release.
class QuickSettings {
 public:
  // `handle`: the status bar that pulls the shade down.
  void create(lv_obj_t *root, lv_obj_t *handle, App *wifiApp);
  bool isOpen() const { return open_; }
  void open();
  void close();
  void refresh();  // time, Wi-Fi name, brightness

 private:
  void setY(int32_t y);  // -kHeight (hidden) .. 0 (open)
  void animateTo(int32_t y);
  void renderBrightness();

  static void onHandle(lv_event_t *e);
  static void onShade(lv_event_t *e);
  static void onScrim(lv_event_t *e);
  static void onWifi(lv_event_t *e);
  static void onBrightness(lv_event_t *e);

  App *wifiApp_ = nullptr;
  lv_obj_t *scrim_ = nullptr, *shade_ = nullptr;
  lv_obj_t *time_ = nullptr, *battery_ = nullptr;
  lv_obj_t *wifiIcon_ = nullptr, *wifiName_ = nullptr;
  lv_obj_t *bar_ = nullptr, *fill_ = nullptr, *pct_ = nullptr;
  bool open_ = false;
  int32_t y_ = 0, pressY_ = 0, startY_ = 0;
  bool dragged_ = false;
};

}  // namespace apps
