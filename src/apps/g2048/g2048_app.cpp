#include "g2048_app.h"

#include <Arduino.h>
#include <esp_random.h>
#include <stdio.h>

#include <type_traits>

#include "board.h"
#include "config.h"
#include "core/store.h"
#include "ui/theme.h"

namespace apps {

static const AppInfo kInfo = {"g2048", "2048", ICON_GRID_VIEW, G2048_COLOR};
static constexpr char kKey[] = "g2048";
static_assert(std::is_trivially_copyable<g2048::Board>::value, "saved and parked byte for byte");

// Board: 4 × 76 px tiles with 8 px gaps on a 344 px panel.
static constexpr int32_t kTile = 76, kGap = 8, kBoard = 4 * kTile + 5 * kGap;
static constexpr int32_t kBoardX = (LCD_WIDTH - kBoard) / 2, kBoardY = 86;
static constexpr int32_t kSwipeMin = 30;
static constexpr uint32_t kPopMs = 120, kGainMs = 700, kConfirmMs = 3000;

struct TileStyle {
  uint32_t bg, fg;
};
// By exponent: 2 and 4 dark, then the classic warm run, then near-black.
static TileStyle tileStyle(uint8_t e) {
  static const TileStyle kStyles[] = {
      {ui::col::surfaceHi, ui::col::text},  // (empty)
      {0x3A3A3C, 0xEDEDED},                 // 2
      {0x4A4540, 0xF2E8DA},                 // 4
      {0xF2B179, 0x1A1208},                 // 8
      {0xF59563, 0x1A1208},                 // 16
      {0xF67C5F, 0x1A0A06},                 // 32
      {0xF65E3B, 0x1A0A06},                 // 64
      {0xEDCF72, 0x1A1606},                 // 128
      {0xEDCC61, 0x1A1606},                 // 256
      {0xEDC850, 0x1A1606},                 // 512
      {0xEDC53F, 0x1A1606},                 // 1024
      {0xEDC22E, 0x1A1606},                 // 2048
  };
  if (e < sizeof kStyles / sizeof kStyles[0]) return kStyles[e];
  return {0x1E1C14, 0xEDC22E};  // beyond 2048
}

static const lv_font_t *tileFont(uint8_t e) {
  const uint32_t v = g2048::value(e);
  return v < 100 ? &font_digits_44 : v < 1000 ? &font_digits_32 : &font_digits_24;
}

static float cellX(int cell) { return float(kGap + (cell % g2048::kSide) * (kTile + kGap)); }
static float cellY(int cell) { return float(kGap + (cell / g2048::kSide) * (kTile + kGap)); }

const AppInfo &G2048App::info() const { return kInfo; }

void G2048App::onWake(Wake wake) {
  rng_.s = esp_random();
  Saved s;
  if (core::store::load(kKey, &s, sizeof s)) saved_ = s;
  if (wake != Wake::Cold && core::store::unpark(kKey, &s, sizeof s)) saved_ = s;  // fresher than NVS
  loaded_ = true;
}

void G2048App::save() {
  if (loaded_) core::store::save(kKey, &saved_, sizeof saved_);
}

// RTC memory for a sleep; NVS only at a power-off (flash wear: never at
// every sleep, ARCHITECTURE §8.5). Closing the app saves too.
void G2048App::onSleep(sleepplan::Kind kind) {
  finishAnim();
  core::store::park(kKey, &saved_, sizeof saved_);
  if (kind == sleepplan::Kind::PowerOff) save();
}

void G2048App::setBoard(const g2048::Board &b) {
  finishAnim();
  closeOverlay();
  saved_.board = b;
  saved_.winSeen = b.won;
  if (board_) buildTiles();
  syncScore();
}

void G2048App::newGame() {
  finishAnim();
  closeOverlay();
  g2048::newGame(saved_.board, rng_);
  saved_.winSeen = 0;
  armNewGame(false);
  if (board_) buildTiles();
  syncScore();
}

static lv_obj_t *chip(lv_obj_t *parent, const char *caption, int32_t x, lv_obj_t **value) {
  lv_obj_t *c = ui::box(parent);
  lv_obj_set_size(c, 124, 56);
  lv_obj_set_pos(c, x, 18);
  lv_obj_set_style_radius(c, 14, 0);
  lv_obj_set_style_bg_color(c, ui::c(ui::col::surface), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_t *l = ui::label(c, &font_caption, ui::col::textDim, caption);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 5);
  *value = ui::label(c, &font_digits_24, ui::col::text, "0");
  lv_obj_align(*value, LV_ALIGN_BOTTOM_MID, 0, -4);
  return c;
}

void G2048App::onCreate(lv_obj_t *root) {
  using namespace ui;
  if (!loaded_) onWake(Wake::Cold);
  if (!g2048::tiles(saved_.board)) g2048::newGame(saved_.board, rng_);
  root_ = root;
  chip(root, "SCORE", kBoardX, &scoreVal_);
  chip(root, "BEST", kBoardX + 124 + 10, &bestVal_);
  gain_ = label(root, &font_body_medium, G2048_COLOR, "");
  lv_obj_set_hidden(gain_, true);

  newBtn_ = roundButton(root, ICON_RESTART_ALT, col::text);
  lv_obj_set_pos(newBtn_, kBoardX + kBoard - 56, 18);
  lv_obj_add_event_cb(newBtn_, onNewGame, LV_EVENT_CLICKED, this);

  board_ = box(root);
  lv_obj_set_size(board_, kBoard, kBoard);
  lv_obj_set_pos(board_, kBoardX, kBoardY);
  lv_obj_set_style_radius(board_, 16, 0);
  lv_obj_set_style_bg_color(board_, c(col::surface), 0);
  lv_obj_set_style_bg_opa(board_, LV_OPA_COVER, 0);
  lv_obj_set_clickable(board_, true);
  lv_obj_add_event_cb(board_, onBoardPress, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(board_, onBoardRelease, LV_EVENT_RELEASED, this);
  for (int i = 0; i < g2048::kCells; i++) {
    lv_obj_t *cell = box(board_);
    lv_obj_set_size(cell, kTile, kTile);
    lv_obj_set_pos(cell, int32_t(cellX(i)), int32_t(cellY(i)));
    lv_obj_set_style_radius(cell, 10, 0);
    lv_obj_set_style_bg_color(cell, c(col::surfaceHi), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
  }
  anim_ = Anim::None;
  confirming_ = false;
  overlayAction_ = OverlayAction::None;
  buildTiles();
  syncScore();
  if (g2048::over(saved_.board)) showOverlay(false);
}

void G2048App::onDestroy() {
  finishAnim();
  save();
  root_ = board_ = scoreVal_ = bestVal_ = gain_ = newBtn_ = overlay_ = nullptr;
  for (auto &t : tileAt_) t = nullptr;
}

lv_obj_t *G2048App::makeTile(int cell, uint8_t exp) {
  const TileStyle st = tileStyle(exp);
  lv_obj_t *t = ui::box(board_);
  lv_obj_set_style_radius(t, 10, 0);
  lv_obj_set_style_bg_color(t, ui::c(st.bg), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>(g2048::value(exp)));
  lv_obj_t *l = ui::label(t, tileFont(exp), st.fg, buf);
  lv_obj_center(l);
  placeTile(t, cellX(cell), cellY(cell), 1);
  return t;
}

// A tile at (x, y) (its full-size top left in the board), scaled about its centre.
void G2048App::placeTile(lv_obj_t *t, float x, float y, float scale) {
  const float s = kTile * scale;
  lv_obj_set_pos(t, int32_t(x + (kTile - s) / 2 + 0.5f), int32_t(y + (kTile - s) / 2 + 0.5f));
  lv_obj_set_size(t, int32_t(s + 0.5f), int32_t(s + 0.5f));
}

void G2048App::buildTiles() {
  for (auto &t : tileAt_) {
    if (t) lv_obj_delete(t);
    t = nullptr;
  }
  for (int i = 0; i < g2048::kCells; i++)
    if (saved_.board.cell[i]) tileAt_[i] = makeTile(i, saved_.board.cell[i]);
  if (overlay_) lv_obj_move_foreground(overlay_);
}

void G2048App::syncScore() {
  if (saved_.board.score > saved_.best) saved_.best = saved_.board.score;
  if (!scoreVal_) return;
  ui::setTextFmt(scoreVal_, "%lu", static_cast<unsigned long>(saved_.board.score));
  ui::setTextFmt(bestVal_, "%lu", static_cast<unsigned long>(saved_.best));
}

void G2048App::swipe(g2048::Dir d) {
  if (overlay_) return;
  finishAnim();
  armNewGame(false);
  move_ = g2048::slide(saved_.board, d);
  if (!move_.moved) return;
  spawned_ = g2048::spawn(saved_.board, rng_);
  syncScore();
  if (move_.gain && gain_) {
    ui::setTextFmt(gain_, "+%lu", static_cast<unsigned long>(move_.gain));
    lv_obj_set_hidden(gain_, false);
    gainMs_ = millis();
  }
  if (!board_) return;
  anim_ = Anim::Slide;
  animMs_ = millis();
}

// Ends whatever animation runs: the tiles show the board as it is now.
void G2048App::finishAnim() {
  if (anim_ == Anim::None) return;
  anim_ = Anim::None;
  if (board_) buildTiles();
  if (!overlay_ && board_) {
    if (move_.justWon && !saved_.winSeen)
      showOverlay(true);
    else if (g2048::over(saved_.board))
      showOverlay(false);
  }
}

static float easeOut(float t) { return 1 - (1 - t) * (1 - t); }

void G2048App::onTick(uint32_t nowMs) {
  if (!board_) return;
  if (overlayAction_ != OverlayAction::None) {
    const OverlayAction a = overlayAction_;
    overlayAction_ = OverlayAction::None;
    if (a == OverlayAction::KeepGoing)
      closeOverlay();
    else
      newGame();
  }
  if (gain_ && !lv_obj_is_hidden(gain_)) {
    const uint32_t e = nowMs - gainMs_;
    if (e >= kGainMs) {
      lv_obj_set_hidden(gain_, true);
    } else {
      const float p = float(e) / kGainMs;
      lv_obj_align(gain_, LV_ALIGN_TOP_LEFT, kBoardX + 124 + 10 + 30, int32_t(64 - 40 * p));
      lv_obj_set_style_opa(gain_, lv_opa_t(255 * (1 - p * p)), 0);
    }
  }
  if (confirming_ && int32_t(nowMs - confirmUntilMs_) >= 0) armNewGame(false);
  if (anim_ == Anim::Slide) {
    const uint32_t e = nowMs - animMs_;
    if (e >= G2048_SLIDE_MS) {
      // Merged and new tiles in place; now they pop / grow.
      buildTiles();
      anim_ = Anim::Pop;
      animMs_ = nowMs;
    } else {
      const float p = easeOut(float(e) / G2048_SLIDE_MS);
      for (int i = 0; i < move_.nSlides; i++) {
        const g2048::Slide &s = move_.slides[i];
        if (lv_obj_t *t = tileAt_[s.from])
          placeTile(t, cellX(s.from) + (cellX(s.to) - cellX(s.from)) * p,
                    cellY(s.from) + (cellY(s.to) - cellY(s.from)) * p, 1);
      }
      return;
    }
  }
  if (anim_ == Anim::Pop) {
    const uint32_t e = nowMs - animMs_;
    const float p = e >= kPopMs ? 1.0f : float(e) / kPopMs;
    for (int i = 0; i < move_.nMerged; i++) {
      const int c = move_.mergedAt[i];
      if (tileAt_[c]) placeTile(tileAt_[c], cellX(c), cellY(c), 1 + 0.12f * (p < 0.5f ? p * 2 : (1 - p) * 2));
    }
    if (spawned_ >= 0 && tileAt_[spawned_]) placeTile(tileAt_[spawned_], cellX(spawned_), cellY(spawned_), easeOut(p));
    if (e >= kPopMs) finishAnim();
  }
}

void G2048App::onBoardPress(lv_event_t *e) {
  auto *self = static_cast<G2048App *>(lv_event_get_user_data(e));
  lv_indev_get_point(lv_indev_active(), &self->pressAt_);
  self->pressed_ = true;
}

void G2048App::onBoardRelease(lv_event_t *e) {
  auto *self = static_cast<G2048App *>(lv_event_get_user_data(e));
  if (!self->pressed_) return;
  self->pressed_ = false;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  const int32_t dx = p.x - self->pressAt_.x, dy = p.y - self->pressAt_.y;
  const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ax < kSwipeMin && ay < kSwipeMin) return;
  if (ax > ay)
    self->swipe(dx < 0 ? g2048::Dir::Left : g2048::Dir::Right);
  else
    self->swipe(dy < 0 ? g2048::Dir::Up : g2048::Dir::Down);
}

// New game: straight away when nothing is lost, else a second tap within 3 s.
void G2048App::onNewGame(lv_event_t *e) {
  auto *self = static_cast<G2048App *>(lv_event_get_user_data(e));
  const bool fresh = self->saved_.board.score == 0 || g2048::over(self->saved_.board);
  if (self->confirming_ || fresh)
    self->newGame();
  else
    self->armNewGame(true);
}

void G2048App::armNewGame(bool on) {
  confirming_ = on;
  confirmUntilMs_ = millis() + kConfirmMs;
  if (!newBtn_) return;
  lv_obj_set_style_bg_color(newBtn_, ui::c(on ? ui::col::warning : ui::col::surface), 0);
  lv_obj_set_style_text_color(newBtn_, ui::c(on ? ui::col::bg : ui::col::text), 0);
}

void G2048App::showOverlay(bool won) {
  using namespace ui;
  closeOverlay();
  if (won) saved_.winSeen = 1;
  overlay_ = box(board_);
  lv_obj_set_size(overlay_, kBoard, kBoard);
  lv_obj_set_style_radius(overlay_, 16, 0);
  lv_obj_set_style_bg_color(overlay_, c(col::bg), 0);
  lv_obj_set_style_bg_opa(overlay_, LV_OPA_80, 0);
  lv_obj_t *t =
      label(overlay_, won ? &font_digits_80 : &font_title, won ? G2048_COLOR : col::text, won ? "2048" : "Game over");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -44);
  lv_obj_t *b = textButton(overlay_, won ? "Keep going" : "New game", won ? col::text : G2048_COLOR);
  lv_obj_set_width(b, 200);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, 44);
  lv_obj_add_event_cb(b, onOverlayButton, LV_EVENT_CLICKED, this);
  lv_obj_set_user_data(b, reinterpret_cast<void *>(intptr_t(won)));
}

void G2048App::closeOverlay() {
  if (overlay_) lv_obj_delete(overlay_);
  overlay_ = nullptr;
}

// The button is inside the overlay it closes: act on the next tick, not
// while LVGL is still dispatching its event.
void G2048App::onOverlayButton(lv_event_t *e) {
  auto *self = static_cast<G2048App *>(lv_event_get_user_data(e));
  self->overlayAction_ = ui::eventIndex(e) != 0 ? OverlayAction::KeepGoing : OverlayAction::NewGame;
}

}  // namespace apps
