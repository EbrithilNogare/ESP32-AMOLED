#pragma once
#include <dice_core.h>
#include <wire_raster.h>

#include "apps/app.h"

namespace apps {

// Dice (docs/apps/dice.md): wireframe d4–d100 that spin and land face-on.
// Roll logic and geometry are lib/DiceCore; this class lays out, animates and
// draws them. The edges go into a stage-sized RGB565 buffer in PSRAM with
// lib/WireRaster, once per frame (LVGL's own line drawing was ~2 fps for
// 2 × 10 d20); the stage's draw event copies that buffer and draws the landed
// faces, numbers and totals on top with LVGL. Only the stage area is redrawn
// while they spin.
class DiceApp : public App {
 public:
  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;

  uint8_t die() const { return die_; }
  uint8_t count() const { return count_; }
  dice::Mode mode() const { return mode_; }
  bool rolling() const { return animating_; }

 private:
  struct Piece {
    uint8_t set, die;  // die: solid to draw (10 for each half of a d100)
    char text[4];      // number shown on the landed face
    float x, y, r;
    dice::Vec3 axis;
    float turns;          // spin in radians, decays to 0
    uint16_t delay, len;  // ms after the roll starts
    float scale;          // landing "pop"
    float reveal;         // 0..1 face fill + number
    float spin;           // 0..1 progress (1 = landed)
    dice::Point p[dice::kMaxVerts];
    uint8_t order[dice::kMaxEdges];  // edges sorted back to front
  };

  void setDie(uint8_t die);
  void setCount(int count);
  void setMode(dice::Mode mode);
  void startRoll();
  void layout();
  void update(uint32_t elapsedMs);
  bool finished() const;
  uint32_t pieceColor(const Piece &pc, bool dropped) const;
  bool droppedNow(const Piece &pc) const;
  void renderWires();
  void syncControls();
  void save();
  void draw(lv_layer_t *layer);
  void drawPiece(lv_layer_t *layer, const Piece &pc, bool dropped);
  void drawSummary(lv_layer_t *layer, bool finished);

  static void onStageDraw(lv_event_t *e);
  static void onStageClick(lv_event_t *e);
  static void onDieClick(lv_event_t *e);
  static void onIconDraw(lv_event_t *e);
  static void onStep(lv_event_t *e);
  static void onModeClick(lv_event_t *e);

  uint8_t die_ = 20, count_ = 1;
  dice::Mode mode_ = dice::Mode::Flat;
  bool loaded_ = false;
  dice::Roll roll_;
  bool idle_ = true;
  bool animating_ = false;
  uint32_t t0_ = 0, endMs_ = 0, nextFrameMs_ = 0;
  unsigned frames_ = 0;  // drawn this roll, for the fps in the debug log
  Piece pieces_[dice::kMaxPieces];
  int nPieces_ = 0;

  // The edges, drawn by renderWires() (stage coordinates: y from kTop).
  raster::Canvas canvas_{};
  lv_draw_buf_t canvasBuf_{};
  struct Box {
    int16_t x0, y0, x1, y1;
  };
  Box drawn_[dice::kMaxPieces] = {};  // where each die was drawn, to clear next frame
  int nDrawn_ = 0;

  lv_obj_t *stage_ = nullptr;
  lv_obj_t *dieBtn_[dice::kTypes] = {};
  lv_obj_t *countLabel_ = nullptr, *minus_ = nullptr, *plus_ = nullptr, *modes_ = nullptr;
};

}  // namespace apps
