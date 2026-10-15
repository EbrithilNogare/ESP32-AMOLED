#include "theme.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ui {

static lv_style_t stScreen, stBox, stButton, stPressed;
static lv_style_transition_dsc_t pressTrans;
static const lv_style_prop_t kPressProps[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_BG_COLOR,
                                              LV_STYLE_PROP_INV};

uint32_t mixBlack(uint32_t color, uint8_t percent) {
  return hex(lv_color_mix(lv_color_hex(color), lv_color_black(), uint8_t(percent * 255 / 100)));
}

void begin() {
  lv_style_init(&stScreen);
  lv_style_set_bg_color(&stScreen, c(col::bg));
  lv_style_set_bg_opa(&stScreen, LV_OPA_COVER);
  lv_style_set_text_color(&stScreen, c(col::text));
  lv_style_set_text_font(&stScreen, &font_body);

  lv_style_init(&stBox);
  lv_style_set_bg_opa(&stBox, LV_OPA_TRANSP);
  lv_style_set_border_width(&stBox, 0);
  lv_style_set_pad_all(&stBox, 0);
  lv_style_set_radius(&stBox, 0);

  lv_style_init(&stButton);
  lv_style_set_bg_color(&stButton, c(col::surface));
  lv_style_set_bg_opa(&stButton, LV_OPA_COVER);
  lv_style_set_radius(&stButton, LV_RADIUS_CIRCLE);
  lv_style_set_border_width(&stButton, 0);
  lv_style_set_pad_hor(&stButton, 22);
  lv_style_set_pad_ver(&stButton, 0);
  lv_style_set_pad_column(&stButton, 8);
  lv_style_set_text_font(&stButton, &font_body_medium);

  lv_style_transition_dsc_init(&pressTrans, kPressProps, lv_anim_path_ease_out, motion::fast, 0, nullptr);
  lv_style_init(&stPressed);
  lv_style_set_transform_scale_x(&stPressed, 240);  // 94 %
  lv_style_set_transform_scale_y(&stPressed, 240);
  lv_style_set_transform_pivot_x(&stPressed, LV_PCT(50));
  lv_style_set_transform_pivot_y(&stPressed, LV_PCT(50));
  lv_style_set_transition(&stPressed, &pressTrans);
}

lv_obj_t *screen() {
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_add_style(s, &stScreen, 0);
  lv_obj_set_scrollable(s, false);
  return s;
}

lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_add_style(o, &stBox, 0);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_clickable(o, false);  // taps fall through to the parent unless opted in
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *header(lv_obj_t *parent, const char *text, uint32_t color) {
  lv_obj_t *l = label(parent, &font_caption, color, text);
  lv_obj_set_style_text_letter_space(l, 1, 0);
  lv_obj_set_pos(l, kEdge, kEdge);
  return l;
}

void pressable(lv_obj_t *obj) {
  lv_obj_add_style(obj, &stPressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(obj, c(col::surfaceHi), LV_STATE_PRESSED);
}

static lv_obj_t *button(lv_obj_t *parent, uint32_t color, int32_t height = kBtn) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_add_style(b, &stButton, 0);
  lv_obj_set_height(b, height);
  lv_obj_set_style_text_color(b, c(color), 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_ext_click_area(b, 6);
  pressable(b);
  return b;
}

lv_obj_t *pill(lv_obj_t *parent, const char *icon, const char *text, uint32_t color) {
  lv_obj_t *b = button(parent, color);
  lv_obj_set_width(b, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(lv_label_create(b), &font_icon_28, 0);  // child 0: icon
  lv_label_create(b);                                                // child 1: text
  setPillContent(b, icon, text);
  return b;
}

void setPillContent(lv_obj_t *pill, const char *icon, const char *text) {
  setText(lv_obj_get_child(pill, 0), icon);
  setText(lv_obj_get_child(pill, 1), text);
}

lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t color, int32_t height) {
  lv_obj_t *b = button(parent, color, height);
  lv_obj_set_width(b, LV_SIZE_CONTENT);
  lv_label_create(b);
  lv_label_set_text(lv_obj_get_child(b, 0), text);
  return b;
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *icon, uint32_t color) {
  lv_obj_t *b = button(parent, color);
  lv_obj_set_width(b, 56);
  lv_obj_set_style_pad_hor(b, 0, 0);
  lv_obj_t *i = lv_label_create(b);
  lv_obj_set_style_text_font(i, &font_icon_28, 0);
  lv_label_set_text(i, icon);
  return b;
}

lv_obj_t *segmented(lv_obj_t *parent, const char *const *labels, int n, const lv_font_t *font, int32_t segWidth,
                    lv_event_cb_t cb, void *user) {
  lv_obj_t *bar = box(parent);
  lv_obj_set_size(bar, LV_SIZE_CONTENT, kBtnSmall);
  lv_obj_set_style_pad_all(bar, 4, 0);
  lv_obj_set_style_pad_column(bar, 2, 0);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(bar, c(col::surface), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < n; i++) {
    lv_obj_t *s = box(bar);
    lv_obj_set_size(s, segWidth ? segWidth : LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_pad_hor(s, 14, 0);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(s, font, 0);
    lv_obj_set_clickable(s, true);
    lv_obj_set_ext_click_area(s, 4);
    lv_obj_set_user_data(s, reinterpret_cast<void *>(intptr_t(i)));
    lv_obj_add_event_cb(s, cb, LV_EVENT_CLICKED, user);
    pressable(s);
    lv_obj_center(lv_label_create(s));  // inherits font and colour from the segment
    lv_label_set_text(lv_obj_get_child(s, 0), labels[i]);
  }
  return bar;
}

void setSegment(lv_obj_t *seg, int selected, lv_color_t fill, lv_color_t text) {
  const int n = int(lv_obj_get_child_count(seg));
  for (int i = 0; i < n; i++) {
    lv_obj_t *s = lv_obj_get_child(seg, i);
    const bool on = i == selected;
    lv_obj_set_style_bg_color(s, on ? fill : c(col::surface), 0);
    lv_obj_set_style_bg_opa(s, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s, on ? text : c(col::textDim), 0);
  }
}

lv_obj_t *toast(lv_obj_t *parent, const char *text) {
  lv_obj_t *t = label(parent, &font_caption, col::text, text);
  lv_obj_set_style_bg_color(t, c(col::surfaceHi), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(t, 12, 0);
  lv_obj_set_style_pad_hor(t, 14, 0);
  lv_obj_set_style_pad_ver(t, 8, 0);
  return t;
}

void flash(lv_obj_t *parent, uint32_t color) {
  lv_obj_t *f = box(parent);  // not clickable: taps go through
  lv_obj_set_size(f, lv_obj_get_width(parent), lv_obj_get_height(parent));
  lv_obj_set_style_bg_color(f, c(color), 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, f);
  lv_anim_set_values(&a, 0, 90);  // ~35 % at the peak
  lv_anim_set_duration(&a, 400);
  lv_anim_set_playback_duration(&a, 400);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, lv_opa_t(v), 0); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_obj_delete((lv_obj_t *)an->var); });
  lv_anim_start(&a);
}

lv_point_t drift(int32_t step, int32_t ax, int32_t ay) {
  return {int32_t(lroundf(sinf(step * 1.7f) * ax)), int32_t(lroundf(cosf(step * 1.1f) * ay))};
}

lv_obj_t *progressRing(lv_obj_t *parent, int32_t size, int32_t width) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 1000);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, c(col::track), LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
  lv_obj_set_clickable(a, false);
  return a;
}

void setText(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(label, text);
}

void setTextFmt(lv_obj_t *label, const char *fmt, ...) {
  char buf[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  setText(label, buf);
}

}  // namespace ui
