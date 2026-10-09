#include "bot_face_app.h"

#include <Arduino.h>
#include <esp_random.h>

#include "board.h"
#include "config.h"
#include "ui/theme.h"

namespace apps {

static const AppInfo kInfo = {"bot_face", "Bot face", ICON_SENTIMENT_SATISFIED, BOT_FACE_COLOR};
static constexpr int kMouthPts = 13;  // samples along the Bézier
static constexpr int32_t kMouthStroke = 12;

BotFaceApp::BotFaceApp(bool (*isDimmed)()) : isDimmed_(isDimmed), face_(esp_random(), 0) {}

const AppInfo &BotFaceApp::info() const { return kInfo; }

static lv_obj_t *shape(lv_obj_t *parent) {
  lv_obj_t *o = ui::box(parent);
  lv_obj_set_style_bg_color(o, ui::c(BOT_FACE_COLOR), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_clickable(o, false);
  return o;
}

void BotFaceApp::onCreate(lv_obj_t *root) {
  face_ = bot::Face(esp_random(), millis());
  eyeL_ = shape(root);
  eyeR_ = shape(root);
  mouthO_ = shape(root);
  lv_obj_set_style_radius(mouthO_, LV_RADIUS_CIRCLE, 0);

  mouth_ = lv_line_create(root);
  lv_obj_set_style_line_color(mouth_, ui::c(BOT_FACE_COLOR), 0);
  lv_obj_set_style_line_width(mouth_, kMouthStroke, 0);
  lv_obj_set_style_line_rounded(mouth_, true, 0);
  lv_obj_set_size(mouth_, LCD_WIDTH, LCD_HEIGHT);  // points are in screen coordinates

  lv_obj_set_clickable(root, true);
  lv_obj_add_event_cb(root, onPressed, LV_EVENT_PRESSED, this);
  nextFrameMs_ = 0;
  draw(millis());
}

void BotFaceApp::onDestroy() { eyeL_ = eyeR_ = mouth_ = mouthO_ = nullptr; }

void BotFaceApp::onPressed(lv_event_t *e) { static_cast<BotFaceApp *>(lv_event_get_user_data(e))->face_.tap(millis()); }

void BotFaceApp::onTick(uint32_t nowMs) {
  if (nowMs < nextFrameMs_ || !eyeL_) return;
  const bool dim = isDimmed_ && isDimmed_();
  nextFrameMs_ = nowMs + 1000 / (dim ? BOT_FACE_FPS_DIM : BOT_FACE_FPS);
  face_.update(nowMs, dim);
  draw(nowMs);
}

static void placeRect(lv_obj_t *o, const bot::Rect &r) {
  lv_obj_set_pos(o, lroundf(r.x), lroundf(r.y));
  lv_obj_set_size(o, lroundf(r.w), lroundf(r.h));
  lv_obj_set_style_radius(o, lroundf(r.r), 0);
}

void BotFaceApp::draw(uint32_t nowMs) {
  const bot::Geometry g = face_.geometry(nowMs);
  placeRect(eyeL_, g.eyeL);
  placeRect(eyeR_, g.eyeR);

  if (g.mouthIsO) {
    lv_obj_set_hidden(mouth_, true);
    lv_obj_set_hidden(mouthO_, false);
    lv_obj_set_pos(mouthO_, lroundf(g.oCx - g.oRx), lroundf(g.oCy - g.oRy));
    lv_obj_set_size(mouthO_, lroundf(2 * g.oRx), lroundf(2 * g.oRy));
    return;
  }
  lv_obj_set_hidden(mouthO_, true);
  lv_obj_set_hidden(mouth_, false);
  // Quadratic Bézier from (mx-44, my) via (mx, my+56·curve) to (mx+44, my).
  const float x0 = g.mx - 44, x2 = g.mx + 44, y0 = g.my, y1 = g.my + 56 * g.curve;
  for (int i = 0; i < kMouthPts; i++) {
    const float t = float(i) / (kMouthPts - 1), u = 1 - t;
    mouthPts_[i].x = u * u * x0 + 2 * u * t * g.mx + t * t * x2;
    mouthPts_[i].y = u * u * y0 + 2 * u * t * y1 + t * t * y0;
  }
  lv_line_set_points(mouth_, mouthPts_, kMouthPts);
}

}  // namespace apps
