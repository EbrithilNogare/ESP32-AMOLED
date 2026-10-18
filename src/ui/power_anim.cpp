#include "power_anim.h"

#include <lvgl.h>

#include "config.h"
#include "theme.h"

namespace ui::power_anim {

static constexpr int32_t kLine = 4;
static constexpr lv_opa_t kGlowOpa = LV_OPA_60;

// The screen as LVGL lays it out: 448 × 368 while an app has it in landscape.
static int32_t width() { return lv_display_get_horizontal_resolution(nullptr); }
static int32_t height() { return lv_display_get_vertical_resolution(nullptr); }
static int32_t mid() { return height() / 2; }

static constexpr int32_t kRing = 132, kRingWidth = 5, kDot = 10;

static lv_obj_t *layer, *topCover, *bottomCover, *line;
static lv_obj_t *veil, *ring, *symbol, *caption, *dot;
static void (*onDone)();
static bool busy;

static void setCover(void *, int32_t h) {
  lv_obj_set_height(topCover, h);
  lv_obj_set_height(bottomCover, h);
  lv_obj_set_y(bottomCover, height() - h);
}
static void setLineWidth(void *, int32_t w) {
  lv_obj_set_width(line, w);
  lv_obj_set_x(line, (width() - w) / 2);
}
// The glow fades with the line: a shadow is drawn whatever the bg opacity,
// and a lit one left a small blob on the black screen.
static void setLineOpa(void *, int32_t o) {
  lv_obj_set_style_bg_opa(line, lv_opa_t(o), 0);
  lv_obj_set_style_shadow_opa(line, lv_opa_t(o * kGlowOpa / LV_OPA_COVER), 0);
}

static lv_obj_t *black(int32_t y, int32_t h) {
  lv_obj_t *o = box(layer);
  lv_obj_set_pos(o, 0, y);
  lv_obj_set_size(o, width(), h);
  lv_obj_set_style_bg_color(o, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

static void newLayer() {
  if (layer) lv_obj_delete(layer);
  layer = box(lv_layer_top());
  veil = ring = symbol = caption = dot = nullptr;
}

static void build(int32_t coverH, int32_t lineW, lv_opa_t lineOpa) {
  newLayer();
  lv_obj_set_size(layer, width(), height());
  lv_obj_set_clickable(layer, false);
  topCover = black(0, coverH);
  bottomCover = black(height() - coverH, coverH);
  line = box(layer);
  lv_obj_set_style_bg_color(line, c(col::text), 0);
  lv_obj_set_style_radius(line, kLine / 2, 0);
  lv_obj_set_style_shadow_color(line, c(col::text), 0);
  lv_obj_set_style_shadow_width(line, 18, 0);
  lv_obj_set_height(line, kLine);
  lv_obj_set_y(line, mid() - kLine / 2);
  setLineWidth(nullptr, lineW);
  setLineOpa(nullptr, lineOpa);
}

// LVGL replaces a running animation that has the same var and exec_cb, so
// every step gets its own tag as var (the callbacks ignore it).
static int tagCover, tagWidth, tagOpaIn, tagOpaOut, tagVeil, tagWind, tagUnwind, tagSymIn, tagSymOut, tagCapIn,
    tagCapOut, tagDotIn, tagDotOut;

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
  build(0, width(), LV_OPA_TRANSP);
  const uint32_t t = POWER_OFF_ANIM_MS;
  // The covers meet in the middle (behind the line), so no row of the picture
  // is left showing once the line fades.
  anim(&tagCover, setCover, 0, mid(), 0, t * 45 / 100);  // picture squeezes to a line
  anim(&tagOpaIn, setLineOpa, LV_OPA_TRANSP, LV_OPA_COVER, t * 30 / 100, t * 15 / 100);
  anim(&tagWidth, setLineWidth, width(), kLine * 2, t * 45 / 100, t * 30 / 100);  // line shrinks to a dot
  anim(&tagOpaOut, setLineOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 75 / 100, t * 25 / 100, [](lv_anim_t *) {
    busy = false;
    if (onDone) onDone();
  });
}

// ---------------------------------------------------------------- shutdown

static void setVeil(void *, int32_t o) { lv_obj_set_style_bg_opa(veil, lv_opa_t(o), 0); }
static void setWind(void *, int32_t deg) { lv_arc_set_angles(ring, 0, uint32_t(deg)); }
static void setUnwind(void *, int32_t deg) { lv_arc_set_angles(ring, uint32_t(deg), 360); }
static void setSymbolOpa(void *, int32_t o) { lv_obj_set_style_text_opa(symbol, lv_opa_t(o), 0); }
static void setCaptionOpa(void *, int32_t o) { lv_obj_set_style_text_opa(caption, lv_opa_t(o), 0); }
static void setDotOpa(void *, int32_t o) {
  lv_obj_set_style_bg_opa(dot, lv_opa_t(o), 0);
  lv_obj_set_style_shadow_opa(dot, lv_opa_t(o * kGlowOpa / LV_OPA_COVER), 0);
}

void shutdown(void (*done)()) {
  onDone = done;
  busy = true;
  newLayer();
  lv_obj_set_size(layer, width(), height());
  lv_obj_set_clickable(layer, false);
  veil = box(layer);
  lv_obj_set_size(veil, width(), height());
  lv_obj_set_style_bg_color(veil, lv_color_black(), 0);
  setVeil(nullptr, LV_OPA_TRANSP);

  const int32_t cy = height() / 2 - 12;
  ring = lv_arc_create(layer);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, kRing, kRing);
  lv_obj_set_pos(ring, (width() - kRing) / 2, cy - kRing / 2);
  lv_arc_set_rotation(ring, 270);  // 0° at 12 o'clock
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, kRingWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, c(col::error), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
  lv_obj_set_clickable(ring, false);
  lv_arc_set_angles(ring, 0, 0);

  symbol = label(layer, &font_icon_56, col::text, ICON_POWER_SETTINGS_NEW);
  lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, cy - lv_font_get_line_height(&font_icon_56) / 2);
  setSymbolOpa(nullptr, LV_OPA_TRANSP);
  caption = label(layer, &font_caption, col::textDim, "Powering off");
  lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, cy + kRing / 2 + 18);
  setCaptionOpa(nullptr, LV_OPA_TRANSP);
  dot = box(layer);
  lv_obj_set_size(dot, kDot, kDot);
  lv_obj_set_pos(dot, (width() - kDot) / 2, cy - kDot / 2);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, c(col::error), 0);
  lv_obj_set_style_shadow_color(dot, c(col::error), 0);
  lv_obj_set_style_shadow_width(dot, 16, 0);
  setDotOpa(nullptr, LV_OPA_TRANSP);

  const uint32_t t = SHUTDOWN_ANIM_MS;
  anim(&tagVeil, setVeil, LV_OPA_TRANSP, LV_OPA_COVER, 0, t * 25 / 100);  // the picture fades out
  anim(&tagSymIn, setSymbolOpa, LV_OPA_TRANSP, LV_OPA_COVER, t * 12 / 100, t * 18 / 100);
  anim(&tagCapIn, setCaptionOpa, LV_OPA_TRANSP, LV_OPA_COVER, t * 18 / 100, t * 15 / 100);
  anim(&tagWind, setWind, 0, 360, t * 15 / 100, t * 33 / 100);      // the ring winds round
  anim(&tagUnwind, setUnwind, 0, 360, t * 52 / 100, t * 30 / 100);  // ... and unwinds
  anim(&tagSymOut, setSymbolOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 60 / 100, t * 22 / 100);
  anim(&tagCapOut, setCaptionOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 55 / 100, t * 20 / 100);
  anim(&tagDotIn, setDotOpa, LV_OPA_TRANSP, LV_OPA_COVER, t * 76 / 100, t * 8 / 100);  // down to a dot
  anim(&tagDotOut, setDotOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 86 / 100, t * 14 / 100, [](lv_anim_t *) {
    busy = false;
    if (onDone) onDone();
  });
}

// ---------------------------------------------------------------- on

void on() {
  busy = true;
  build(mid() - kLine / 2, kLine * 2, LV_OPA_COVER);
  const uint32_t t = POWER_ON_ANIM_MS;
  anim(&tagWidth, setLineWidth, kLine * 2, width(), 0, t * 40 / 100);  // dot grows into a line
  anim(&tagCover, setCover, mid() - kLine / 2, 0, t * 40 / 100, t * 60 / 100, [](lv_anim_t *) {
    lv_obj_delete(layer);
    layer = nullptr;
    busy = false;
  });
  anim(&tagOpaOut, setLineOpa, LV_OPA_COVER, LV_OPA_TRANSP, t * 50 / 100, t * 40 / 100);
}

bool running() { return busy; }

}  // namespace ui::power_anim
