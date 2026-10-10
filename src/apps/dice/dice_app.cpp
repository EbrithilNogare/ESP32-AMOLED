#include "dice_app.h"

#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <stdio.h>

#include "board.h"
#include "config.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/draw.h"
#include "ui/theme.h"

namespace apps {

using dice::Mode;
namespace draw = ui::draw;

static const AppInfo kInfo = {"dice", "Dice", ICON_CASINO, DICE_COLOR};

// Roll area between the die strip and the bottom bar (screen coordinates).
static constexpr int32_t kTop = 70, kBot = 376;
static constexpr uint32_t kOff = ui::col::textOff;  // idle and dropped dice
static constexpr uint32_t kGhost = 0x050505;        // their face fill
static constexpr uint32_t kSettleMs = 260;          // after the last die lands
static constexpr float kPi = 3.14159265f;

static uint32_t hwRandom() { return esp_random(); }
static float random01() { return float(esp_random() >> 8) / 16777216.0f; }
static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

const AppInfo &DiceApp::info() const { return kInfo; }

// ---------------------------------------------------------------- drawing helpers

// Largest digit font that fits `px`. Only the smaller ones carry "?".
static const lv_font_t *digitFont(float px, bool question) {
  static const struct {
    const lv_font_t *font;
    int px;
  } kFonts[] = {{&font_digits_24, 24},
                {&font_digits_32, 32},
                {&font_digits_44, 44},
                {&font_digits_56, 56},
                {&font_digits_80, 80}};
  const lv_font_t *f = kFonts[0].font;
  for (const auto &k : kFonts)
    if (k.px <= px * 1.15f && (!question || k.px <= 44)) f = k.font;
  return f;
}

// Number size relative to the die radius, so it fits inside the landed face.
static float faceFontScale(uint8_t die) {
  switch (die) {
    case 4: return 0.38f;
    case 6: return 0.56f;
    case 8: return 0.34f;
    case 10: return 0.32f;
    case 12: return 0.44f;
    default: return 0.31f;
  }
}

// ---------------------------------------------------------------- state

void DiceApp::save() {
  const uint8_t s[3] = {die_, count_, uint8_t(mode_)};
  core::store::save("dice", s, sizeof s);
}

void DiceApp::setDie(uint8_t die) {
  die_ = die;
  if (count_ > dice::maxCount(die_)) count_ = uint8_t(dice::maxCount(die_));
  roll_ = dice::idle(die_, count_, mode_);
  idle_ = true;
  animating_ = false;
  layout();
  syncControls();
  save();
}

void DiceApp::setCount(int count) {
  count_ = uint8_t(count < 1 ? 1 : count > dice::maxCount(die_) ? dice::maxCount(die_) : count);
  setDie(die_);
}

void DiceApp::setMode(Mode mode) {
  mode_ = mode;
  setDie(die_);
}

void DiceApp::startRoll() {
  roll_ = dice::roll(die_, count_, mode_, hwRandom);
  idle_ = false;
  layout();
  t0_ = millis();
  animating_ = true;
  nextFrameMs_ = millis();
}

// Places every die (two pieces for a d100) and gives each its own spin.
void DiceApp::layout() {
  const bool multi = roll_.count > 1 || roll_.die == 100;  // leaves room for the total
  const float aspect = roll_.die == 100 ? 1.75f : 1.0f;
  dice::Slot slots[dice::kMaxPieces];
  int n = 0;
  if (roll_.sets == 1) {
    const float y1 = multi ? kBot - 76 : kBot - 40;
    dice::layoutGrid(roll_.count, 24, kTop + 8, LCD_WIDTH - 48, y1 - kTop - 8, multi ? 70 : 112, aspect, slots);
    n = roll_.count;
  } else {
    const float y0 = kTop + 34, y1 = multi ? kBot - 70 : kBot - 44, w = LCD_WIDTH / 2.0f - 22;
    dice::layoutGrid(roll_.count, 16, y0, w, y1 - y0, multi ? 60 : 74, aspect, slots);
    dice::layoutGrid(roll_.count, LCD_WIDTH / 2.0f + 6, y0, w, y1 - y0, multi ? 60 : 74, aspect, slots + roll_.count);
    n = 2 * roll_.count;
  }

  nPieces_ = 0;
  uint32_t end = 0;
  for (int k = 0; k < n; k++) {
    const int set = k / roll_.count, i = k % roll_.count;
    const uint8_t v = roll_.v[set][i];
    const uint16_t delay = uint16_t(k * 80), len = uint16_t(DICE_ROLL_MS + random01() * 300);
    end = max<uint32_t>(end, delay + len);
    auto add = [&](uint8_t solidDie, float dx, float sc, unsigned value) {
      Piece &pc = pieces_[nPieces_++];
      pc.set = uint8_t(set);
      pc.die = solidDie;
      if (idle_)
        snprintf(pc.text, sizeof pc.text, "?");
      else if (roll_.die == 100 && dx < 0)
        snprintf(pc.text, sizeof pc.text, "%02u", value);  // tens die: 00-90
      else
        snprintf(pc.text, sizeof pc.text, "%u", value);
      pc.x = slots[k].x + dx;
      pc.y = slots[k].y;
      pc.r = slots[k].r * sc * 1.08f;
      pc.axis = dice::randomAxis(random01(), random01());
      pc.turns = (2.2f + random01() * 1.6f) * 2 * kPi;
      pc.delay = delay;
      pc.len = len;
    };
    if (roll_.die == 100) {
      const dice::Percentile p = dice::split(v);
      add(10, -slots[k].r * 0.66f, 0.72f, p.tens);
      add(10, slots[k].r * 0.66f, 0.72f, p.ones);
    } else {
      add(roll_.die, 0, 1, v);
    }
  }
  endMs_ = end + kSettleMs;
  update(idle_ ? UINT32_MAX / 2 : 0);
  if (stage_) lv_obj_invalidate(stage_);
}

// Advances every piece to `elapsed` ms after the roll: pose, projection,
// back-to-front edge order, landing pop and face reveal.
void DiceApp::update(uint32_t elapsed) {
  for (int k = 0; k < nPieces_; k++) {
    Piece &pc = pieces_[k];
    const dice::Solid &s = dice::solid(pc.die);
    const float lt = float(elapsed) - pc.delay;
    pc.spin = idle_ ? 1 : clamp01(lt / pc.len);
    const float after = lt - pc.len;
    pc.scale = pc.spin >= 1 && !idle_ ? 1 + 0.06f * expf(-after / 80) * sinf(after / 30) : 1;
    pc.reveal = idle_ ? 1 : clamp01((after + 40) / 180);
    dice::project(s, dice::spinPose(s, pc.axis, pc.turns, pc.spin), pc.x, pc.y, pc.r * pc.scale, pc.p);
    for (int e = 0; e < s.ne; e++) pc.order[e] = uint8_t(e);
    auto depth = [&](int e) { return pc.p[s.e[e][0]].z + pc.p[s.e[e][1]].z; };
    for (int a = 1; a < s.ne; a++)  // insertion sort, deepest first (≤ 30 edges)
      for (int b = a; b > 0 && depth(pc.order[b]) > depth(pc.order[b - 1]); b--) {
        const uint8_t t = pc.order[b];
        pc.order[b] = pc.order[b - 1];
        pc.order[b - 1] = t;
      }
  }
}

// ---------------------------------------------------------------- rendering

void DiceApp::drawPiece(lv_layer_t *layer, const Piece &pc, bool dropped) {
  const dice::Solid &s = dice::solid(pc.die);
  const bool keptLanded = !idle_ && pc.spin >= 1 && pc.set == roll_.kept;
  uint32_t color = idle_ || dropped ? kOff : DICE_COLOR;
  if (keptLanded && roll_.crit == dice::Crit::Nat20) color = ui::col::success;
  if (keptLanded && roll_.crit == dice::Crit::Nat1) color = ui::col::error;
  const float bright = idle_ ? 0.7f : 1.0f;
  const float r = pc.r * pc.scale;

  for (int k = 0; k < s.ne; k++) {
    const uint8_t *e = s.e[pc.order[k]];
    const dice::Point &a = pc.p[e[0]], &b = pc.p[e[1]];
    const float z = (a.z + b.z) / 2, d = clamp01((1 - z) / 2);
    const bool front = z < 0;
    const float opa = (front ? 0.55f + 0.45f * d : 0.14f + 0.2f * d) * bright;
    const int32_t width = front ? int32_t(fmaxf(2, r * 0.028f) + 0.5f) : int32_t(fmaxf(1, r * 0.016f) + 0.5f);
    draw::line(layer, a.x, a.y, b.x, b.y, color, width, lv_opa_t(opa * 255));
  }
  if (pc.reveal <= 0) return;

  // Landed: fill the face toward the viewer (a fan; faces are convex),
  // outline it again on top and put the number in the middle.
  const lv_opa_t opa = lv_opa_t(pc.reveal * 255);
  const uint32_t fillColor = idle_ || dropped ? kGhost : ui::mixBlack(DICE_COLOR, 15);
  float face[dice::kMaxFaceVerts][2];
  for (int k = 0; k < s.nface; k++) face[k][0] = pc.p[s.face[k]].x, face[k][1] = pc.p[s.face[k]].y;
  draw::polygon(layer, face, s.nface, fillColor, opa);
  const int32_t outline = int32_t(fmaxf(2, r * 0.032f) + 0.5f);
  for (int k = 0; k < s.nface; k++) {
    const dice::Point &a = pc.p[s.face[k]], &b = pc.p[s.face[(k + 1) % s.nface]];
    draw::line(layer, a.x, a.y, b.x, b.y, color, outline, opa);
  }
  const size_t len = strlen(pc.text);
  const float px = r * faceFontScale(pc.die) * (len >= 3 ? 0.62f : len == 2 ? 0.8f : 1.0f);
  const uint32_t textColor = idle_ || dropped                               ? kOff
                             : keptLanded && roll_.crit != dice::Crit::None ? color
                                                                            : ui::col::text;
  draw::text(layer, digitFont(px, idle_), textColor, opa, pc.text, int32_t(pc.x), int32_t(pc.y + px * 0.05f));
}

void DiceApp::drawSummary(lv_layer_t *layer, bool finished) {
  if (idle_) {
    draw::text(layer, &font_body, ui::col::textDim, LV_OPA_COVER, roll_.sets == 2 ? "Tap to roll twice" : "Tap to roll",
               LCD_WIDTH / 2, kBot - 20);
    return;
  }
  const uint32_t crit = roll_.crit == dice::Crit::Nat20  ? ui::col::success
                        : roll_.crit == dice::Crit::Nat1 ? ui::col::error
                                                         : 0;
  const char *critText = roll_.crit == dice::Crit::Nat20 ? "NATURAL 20" : "NATURAL 1";
  char buf[8];
  const bool showTotal = roll_.count > 1 || roll_.die == 100;  // a d100 is read as tens + ones
  if (roll_.sets == 1) {
    if (showTotal && finished) {
      draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER, "TOTAL", LCD_WIDTH / 2, kBot - 64, 1);
      snprintf(buf, sizeof buf, "%u", roll_.total[0]);
      draw::text(layer, &font_digits_44, ui::col::text, LV_OPA_COVER, buf, LCD_WIDTH / 2, kBot - 30);
    } else if (finished && crit) {
      draw::text(layer, &font_caption, crit, LV_OPA_COVER, critText, LCD_WIDTH / 2, kBot - 20, 1);
    }
    return;
  }
  for (int s = 0; s < 2; s++) {
    const int32_t x = s ? LCD_WIDTH * 3 / 4 - 2 : LCD_WIDTH / 4 + 2;
    const bool kept = s == roll_.kept;
    const char *lab = !finished ? (s ? "2ND" : "1ST") : kept ? "KEPT" : "DROPPED";
    draw::text(layer, &font_caption,
               !finished ? ui::col::textDim
               : kept    ? DICE_COLOR
                         : kOff,
               LV_OPA_COVER, lab, x, kTop + 16, 1);
    if (showTotal && finished) {
      snprintf(buf, sizeof buf, "%u", roll_.total[s]);
      draw::text(layer, &font_digits_32, kept ? ui::col::text : kOff, LV_OPA_COVER, buf, x, kBot - 46);
      if (!kept) {
        // Roboto Mono: every digit has the same advance.
        const float w = strlen(buf) * lv_font_get_glyph_width(&font_digits_32, '0', 0) + 8;
        draw::line(layer, x - w / 2, kBot - 45, x + w / 2, kBot - 45, kOff, 2, LV_OPA_COVER);
      }
    }
  }
  if (finished) {
    if (crit)
      draw::text(layer, &font_caption, crit, LV_OPA_COVER, critText, LCD_WIDTH / 2, kBot - 14, 1);
    else
      draw::text(layer, &font_caption, ui::col::textDim, LV_OPA_COVER,
                 roll_.mode == Mode::Adv ? "Advantage · higher total kept" : "Disadvantage · lower total kept",
                 LCD_WIDTH / 2, kBot - 14);
  }
}

void DiceApp::draw(lv_layer_t *layer) {
  // Pieces are laid out in screen coordinates of an unshifted root; follow
  // the stage so pixel shift moves the dice too.
  lv_area_t a;
  lv_obj_get_coords(stage_, &a);
  const float dx = float(a.x1), dy = float(a.y1 - kTop);
  const bool finished = !animating_ || millis() - t0_ + kSettleMs >= endMs_;
  for (int k = 0; k < nPieces_; k++) {
    Piece pc = pieces_[k];
    pc.x += dx;
    pc.y += dy;
    for (int i = 0; i < dice::solid(pc.die).nv; i++) pc.p[i].x += dx, pc.p[i].y += dy;
    drawPiece(layer, pc, finished && !idle_ && roll_.sets == 2 && pc.set != roll_.kept);
  }
  drawSummary(layer, finished);
}

// ---------------------------------------------------------------- widgets

void DiceApp::syncControls() {
  if (!stage_) return;
  for (int i = 0; i < dice::kTypes; i++) {
    const bool on = dice::kDice[i] == die_;
    lv_obj_set_style_bg_opa(dieBtn_[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(dieBtn_[i], ui::c(on ? DICE_COLOR : kOff), 0);
  }
  ui::setTextFmt(countLabel_, "%ud%u", count_, die_);
  lv_obj_set_style_text_color(minus_, ui::c(count_ > 1 ? ui::col::text : ui::col::outline), 0);
  lv_obj_set_style_text_color(plus_, ui::c(count_ < dice::maxCount(die_) ? ui::col::text : ui::col::outline), 0);
  static const uint32_t kOn[3] = {DICE_COLOR, ui::col::success, ui::col::error};
  const uint32_t on = kOn[int(mode_)];
  ui::setSegment(modes_, int(mode_), ui::c(on), ui::c(ui::mixBlack(on, 14)));
}

void DiceApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  if (!loaded_) {
    uint8_t s[3];
    if (core::store::load("dice", s, sizeof s) && dice::validDie(s[0]) && s[2] <= 2) {
      die_ = s[0];
      mode_ = Mode(s[2]);
      count_ = uint8_t(s[1] < 1 ? 1 : s[1] > dice::maxCount(die_) ? dice::maxCount(die_) : s[1]);
    }
    loaded_ = true;
  }

  stage_ = box(root);
  lv_obj_set_pos(stage_, 0, kTop);
  lv_obj_set_size(stage_, LCD_WIDTH, kBot - kTop);
  lv_obj_set_clickable(stage_, true);
  lv_obj_add_event_cb(stage_, onStageDraw, LV_EVENT_DRAW_MAIN, this);
  lv_obj_add_event_cb(stage_, onStageClick, LV_EVENT_CLICKED, this);

  // Die strip: icon + name for each die type.
  const float step = (LCD_WIDTH - 2 * kEdge - 42) / float(dice::kTypes - 1);
  for (int i = 0; i < dice::kTypes; i++) {
    lv_obj_t *b = box(root);
    dieBtn_[i] = b;
    lv_obj_set_size(b, 42, 56);
    lv_obj_set_pos(b, int32_t(kEdge + i * step + 0.5f), 9);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, c(mixBlack(DICE_COLOR, 15)), 0);
    lv_obj_set_clickable(b, true);
    pressable(b);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(intptr_t(i)));
    lv_obj_add_event_cb(b, onDieClick, LV_EVENT_CLICKED, this);
    lv_obj_t *ic = box(b);
    lv_obj_set_size(ic, 28, 28);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_user_data(ic, reinterpret_cast<void *>(intptr_t(dice::kDice[i])));
    lv_obj_add_event_cb(ic, onIconDraw, LV_EVENT_DRAW_MAIN, this);
    char name[5];
    snprintf(name, sizeof name, "d%u", dice::kDice[i]);
    lv_obj_t *l = label(b, &font_caption, kOff, name);
    lv_obj_remove_local_style_prop(l, LV_STYLE_TEXT_COLOR, 0);  // inherit the button's colour
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -3);
  }

  // Bottom bar: count stepper and Flat / Adv / Dis.
  lv_obj_t *stepper = box(root);
  lv_obj_set_size(stepper, 142, kBtnSmall);
  lv_obj_align(stepper, LV_ALIGN_BOTTOM_LEFT, kEdge, -kBarBottom);
  lv_obj_set_style_radius(stepper, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(stepper, c(col::surface), 0);
  lv_obj_set_style_bg_opa(stepper, LV_OPA_COVER, 0);
  auto stepButton = [&](const char *txt, int delta, lv_align_t align) {
    lv_obj_t *b = box(stepper);
    lv_obj_set_size(b, 42, kBtnSmall);
    lv_obj_align(b, align, 0, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_clickable(b, true);
    pressable(b);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(intptr_t(delta)));
    lv_obj_add_event_cb(b, onStep, LV_EVENT_CLICKED, this);
    lv_obj_center(label(b, &font_title, col::text, txt));
    return b;
  };
  minus_ = stepButton("\xE2\x88\x92", -1, LV_ALIGN_LEFT_MID);  // U+2212 minus
  plus_ = stepButton("+", 1, LV_ALIGN_RIGHT_MID);
  countLabel_ = label(stepper, &font_body_medium, col::text, "");
  lv_obj_center(countLabel_);

  static const char *const kModes[3] = {"Flat", "Adv", "Dis"};
  modes_ = segmented(root, kModes, 3, &font_caption, 0, onModeClick, this);
  lv_obj_align(modes_, LV_ALIGN_BOTTOM_RIGHT, -kEdge, -kBarBottom);

  roll_ = dice::idle(die_, count_, mode_);
  idle_ = true;
  animating_ = false;
  layout();
  syncControls();
}

void DiceApp::onDestroy() {
  stage_ = countLabel_ = minus_ = plus_ = modes_ = nullptr;
  for (auto &b : dieBtn_) b = nullptr;
  animating_ = false;
}

void DiceApp::onTick(uint32_t nowMs) {
  if (!stage_ || !animating_ || !core::reached(nowMs, nextFrameMs_)) return;
  nextFrameMs_ = nowMs + 1000 / DICE_FPS;
  const uint32_t elapsed = nowMs - t0_;
  if (elapsed >= endMs_) {
    animating_ = false;
    update(UINT32_MAX / 2);
  } else {
    update(elapsed);
  }
  lv_obj_invalidate(stage_);
}

// ---------------------------------------------------------------- events

void DiceApp::onStageDraw(lv_event_t *e) {
  static_cast<DiceApp *>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));
}

void DiceApp::onStageClick(lv_event_t *e) { static_cast<DiceApp *>(lv_event_get_user_data(e))->startRoll(); }

void DiceApp::onDieClick(lv_event_t *e) {
  static_cast<DiceApp *>(lv_event_get_user_data(e))->setDie(dice::kDice[ui::eventIndex(e)]);
}

void DiceApp::onStep(lv_event_t *e) {
  auto *self = static_cast<DiceApp *>(lv_event_get_user_data(e));
  self->setCount(self->count_ + ui::eventIndex(e));
}

void DiceApp::onModeClick(lv_event_t *e) {
  static_cast<DiceApp *>(lv_event_get_user_data(e))->setMode(Mode(ui::eventIndex(e)));
}

// Flat die icon for the strip, in the button's current text colour.
void DiceApp::onIconDraw(lv_event_t *e) {
  lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  const uint8_t die = uint8_t(intptr_t(lv_obj_get_user_data(obj)));
  const uint32_t color = ui::hex(lv_obj_get_style_text_color(lv_obj_get_parent(obj), LV_PART_MAIN));
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  const float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f, k = 12.5f;
  dice::Segment seg[dice::kMaxIconSegments];
  const int n = dice::icon(die, seg);
  lv_layer_t *layer = lv_event_get_layer(e);
  for (int i = 0; i < n; i++)
    draw::line(layer, cx + seg[i].x1 * k, cy + seg[i].y1 * k, cx + seg[i].x2 * k, cy + seg[i].y2 * k, color, 2,
               seg[i].inner ? LV_OPA_50 : LV_OPA_COVER);
}

}  // namespace apps
