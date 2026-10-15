#include "power_anim.h"

#include <lvgl.h>

#include "board.h"
#include "config.h"
#include "theme.h"

namespace ui::power_anim {

static constexpr int32_t kMid = LCD_HEIGHT / 2;
static constexpr int32_t kLine = 4;

static lv_obj_t *layer, *topCover, *bottomCover, *line;
static void (*onDone)();
static bool busy;

static void setCover(void *, int32_t h) {
  lv_obj_set_height(topCover, h);
  lv_obj_set_height(bottomCover, h);
  lv_obj_set_y(bottomCover, LCD_HEIGHT - h);
}
static void setLineWidth(void *, int32_t w) {
  lv_obj_set_width(line, w);
  lv_obj_set_x(line, (LCD_WIDTH - w) / 2);
}
static void setLineOpa(void *, int32_t o) { lv_obj_set_style_bg_opa(line, lv_opa_t(o), 0); }

static lv_obj_t *black(int32_t y, int32_t h) {
  lv_obj_t *o = box(layer);
  lv_obj_set_pos(o, 0, y);
  lv_obj_set_size(o, LCD_WIDTH, h);
  lv_obj_set_style_bg_color(o, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

static void build(int32_t coverH, int32_t lineW, lv_opa_t lineOpa) {
  if (layer) lv_obj_delete(layer);
  layer = box(lv_layer_top());
  lv_obj_set_size(layer, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_clickable(layer, false);
  topCover = black(0, coverH);
  bottomCover = black(LCD_HEIGHT - coverH, coverH);
  line = box(layer);
  lv_obj_set_style_bg_color(line, c(col::text), 0);
  lv_obj_set_style_radius(line, kLine / 2, 0);
  lv_obj_set_style_shadow_color(line, c(col::text), 0);
  lv_obj_set_style_shadow_width(line, 18, 0);
  lv_obj_set_style_shadow_opa(line, LV_OPA_60, 0);
  lv_obj_set_height(line, kLine);
  lv_obj_set_y(line, kMid - kLine / 2);
  setLineWidth(nullptr, lineW);
  setLineOpa(nullptr, lineOpa);
}

// LVGL replaces a running animation that has the same var and exec_cb, so
// every step gets its own tag as var (the callbacks ignore it).
static int tagCover, tagWidth, tagOpaIn, tagOpaOut;

static void anim(void *tag, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t delay, uint32_t ms,
                 lv_anim_completed_cb_t done = nullptr) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, tag);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_early_apply(&a, false);  // delayed steps must not jump to their start value early
  if (done) lv_anim_set_completed_cb(&a, done);
  lv_anim_start(&a);
}

void off(void (*done)()) {
  onDone = done;
  busy = true;
  build(0, LCD_WIDTH, LV_OPA_TRANSP);
  const uint32_t t = POWER_OFF_ANIM_MS;
  anim(&tagCover, setCover, 0, kMid - kLine / 2, 0, t * 45 / 100);  // picture squeezes to a line
  anim(&tagOpaIn, setLineOpa, LV_OPA_TRANSP, LV_OPA_COVER, t * 30 / 100, t * 15 / 100);
  anim(&tagWidth, setLineWidth, LCD_WIDTH, kLine * 2, t * 45 / 100, t * 30 / 100);  // line shrinks to a dot
  anim(&tagOpaOut, setLineOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 75 / 100, t * 25 / 100, [](lv_anim_t *) {
    busy = false;
    if (onDone) onDone();
  });
}

void on() {
  busy = true;
  build(kMid - kLine / 2, kLine * 2, LV_OPA_COVER);
  const uint32_t t = POWER_ON_ANIM_MS;
  anim(&tagWidth, setLineWidth, kLine * 2, LCD_WIDTH, 0, t * 40 / 100);  // dot grows into a line
  anim(&tagCover, setCover, kMid - kLine / 2, 0, t * 40 / 100, t * 60 / 100, [](lv_anim_t *) {
    lv_obj_delete(layer);
    layer = nullptr;
    busy = false;
  });
  anim(&tagOpaOut, setLineOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 50 / 100, t * 40 / 100);
}

bool running() { return busy; }

}  // namespace ui::power_anim
