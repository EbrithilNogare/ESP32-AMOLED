#pragma once
#include <game_2048.h>

#include "apps/app.h"

namespace apps {

// 2048 (docs/apps/g2048.md): swipe on the board. The rules are lib/Game2048;
// this class keeps one LVGL object per tile and animates a move in two short
// steps, driven from onTick(): the tiles slide (positions), then the merged
// ones pop and the new one grows (sizes about their centres, no transforms).
class G2048App : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;

  void swipe(g2048::Dir d);  // what a swipe on the board does (also for the simulator)
  const g2048::Board &board() const { return saved_.board; }
  uint32_t best() const { return saved_.best; }
  bool overlayShown() const { return overlay_ != nullptr; }
  void setBoard(const g2048::Board &b);  // simulator: a given position

 private:
  struct Saved {
    g2048::Board board;
    uint32_t best;
    uint8_t winSeen;  // the 2048 overlay was shown for this game
    uint8_t pad[3];
  };
  enum class Anim : uint8_t { None, Slide, Pop };
  enum class OverlayAction : uint8_t { None, KeepGoing, NewGame };

  void newGame();
  void save();
  void buildTiles();  // tiles from the board, no animation
  lv_obj_t *makeTile(int cell, uint8_t exp);
  void placeTile(lv_obj_t *t, float x, float y, float scale);
  void finishAnim();
  void syncScore();
  void showOverlay(bool won);
  void closeOverlay();
  void armNewGame(bool on);

  static void onBoardPress(lv_event_t *e);
  static void onBoardRelease(lv_event_t *e);
  static void onNewGame(lv_event_t *e);
  static void onOverlayButton(lv_event_t *e);

  Saved saved_{};
  g2048::Rng rng_{0};
  bool loaded_ = false;

  Anim anim_ = Anim::None;
  uint32_t animMs_ = 0;
  g2048::Move move_{};
  int spawned_ = -1;
  uint32_t gainMs_ = 0;  // the "+N" floating up
  uint32_t confirmUntilMs_ = 0;
  bool confirming_ = false;
  lv_point_t pressAt_{};
  bool pressed_ = false;
  OverlayAction overlayAction_ = OverlayAction::None;

  lv_obj_t *root_ = nullptr, *board_ = nullptr, *scoreVal_ = nullptr, *bestVal_ = nullptr, *gain_ = nullptr;
  lv_obj_t *newBtn_ = nullptr, *overlay_ = nullptr;
  lv_obj_t *tileAt_[g2048::kCells] = {};  // the tile object in each cell (before a move: where it starts)
};

}  // namespace apps
