#pragma once
#include <ha_parse.h>

#include "apps/app.h"
#include "ha_client.h"

namespace apps {

// Home Assistant lights (docs/apps/home-assistant.md): every light.* entity,
// loaded from Home Assistant, as tiles: a 2x2 grid for up to four, a
// scrolling two-column grid for more. Tap toggles; states poll while open.
class HomeAssistantApp : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void onSleep(sleepplan::Kind kind) override;

  // From Ev::HaResult; `toggle` = the answer to a tap rather than a poll,
  // `gen` = the list generation the request was made for.
  void onResult(int index, ha::LightState state, ha::Result result, bool toggle, uint8_t gen);
  void onLights(ha::Result result);  // from Ev::HaLights
  void onWifiChanged();
  bool pending(int index) const { return tiles_[index].pending; }
  int tileCount() const { return nTiles_; }

 private:
  struct Tile {
    lv_obj_t *obj = nullptr, *icon = nullptr, *name = nullptr, *state = nullptr, *spinner = nullptr;
    ha::LightState value = ha::LightState::Unknown;
    bool pending = false;
    bool stale = true;
  };
  void buildTiles();
  void renderTile(int i);
  void updateBanner();
  void noteResult(ha::Result r, uint32_t nowMs);
  static void shake(lv_obj_t *obj);
  static void onTileClicked(lv_event_t *e);

  Tile tiles_[ha_client::kMaxLights];
  int nTiles_ = 0;
  lv_obj_t *root_ = nullptr, *grid_ = nullptr, *banner_ = nullptr, *empty_ = nullptr;
  bool open_ = false;
  bool clientStarted_ = false;
  bool listLoaded_ = false;              // answered by Home Assistant since the app opened
  ha::Result problem_ = ha::Result::Ok;  // what the banner explains
  uint32_t nextPollMs_ = 0, nextListMs_ = 0, retryMs_ = 0;
};

}  // namespace apps
