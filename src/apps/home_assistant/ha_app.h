#pragma once
#include <ha_parse.h>

#include "apps/app.h"
#include "ha_client.h"

namespace apps {

// Home Assistant lights (docs/apps/home-assistant.md): a 2x2 grid of the four
// configured lights. Tap toggles; states poll while the app is open.
class HomeAssistantApp : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void onSleep() override;

  void onResult(int index, ha::LightState state, ha::Result result);  // from Ev::HaResult
  void onWifiChanged();

 private:
  struct Tile {
    lv_obj_t *obj = nullptr, *icon = nullptr, *name = nullptr, *state = nullptr, *spinner = nullptr;
    ha::LightState value = ha::LightState::Unknown;
    bool pending = false;
    bool stale = true;
  };
  void renderTile(int i);
  void setBanner(const char *text, uint32_t color);
  void updateBanner();
  void shake(lv_obj_t *obj);
  static void onTileClicked(lv_event_t *e);

  Tile tiles_[ha_client::kLights];
  lv_obj_t *banner_ = nullptr;
  bool open_ = false;
  bool unauthorized_ = false;
  bool clientStarted_ = false;
  uint32_t nextPollMs_ = 0;
};

}  // namespace apps
