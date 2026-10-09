#include "theme.h"

namespace ui {

static lv_style_t stScreen, stBox, stButton, stPressed;
static lv_style_transition_dsc_t pressTrans;
static const lv_style_prop_t kPressProps[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_BG_COLOR,
                                              LV_STYLE_PROP_INV};

lv_color_t mixBlack(uint32_t hex, uint8_t percent) {
  return lv_color_mix(lv_color_hex(hex), lv_color_black(), uint8_t(percent * 255 / 100));
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
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  lv_label_set_text(l, text);
  return l;
}

void pressable(lv_obj_t *obj) {
  lv_obj_add_style(obj, &stPressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(obj, c(col::surfaceHi), LV_STATE_PRESSED);
}

static lv_obj_t *button(lv_obj_t *parent, uint32_t color) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_add_style(b, &stButton, 0);
  lv_obj_set_height(b, 56);
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
  lv_label_set_text(lv_obj_get_child(pill, 0), icon);
  lv_label_set_text(lv_obj_get_child(pill, 1), text);
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

}  // namespace ui
