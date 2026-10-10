#include "draw.h"

#include "board.h"
#include "theme.h"

namespace ui::draw {

void text(lv_layer_t *layer, const lv_font_t *font, uint32_t color, lv_opa_t opa, const char *txt, int32_t cx,
          int32_t cy, int32_t letterSpace, lv_text_align_t align) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = font;
  d.color = c(color);
  d.opa = opa;
  d.text = txt;
  d.text_local = 1;  // the draw task runs later; let LVGL copy the text
  d.align = align;
  d.letter_space = letterSpace;
  // One line in a box as wide as the screen; cx is its centre, or the left /
  // right edge for left / right alignment.
  constexpr int32_t w = LCD_WIDTH;
  const int32_t h = lv_font_get_line_height(font);
  const int32_t x1 = align == LV_TEXT_ALIGN_LEFT ? cx : align == LV_TEXT_ALIGN_RIGHT ? cx - w : cx - w / 2;
  const lv_area_t a = {x1, cy - h / 2, x1 + w - 1, cy + h - h / 2};
  lv_draw_label(layer, &d, &a);
}

void line(lv_layer_t *layer, float x1, float y1, float x2, float y2, uint32_t color, int32_t width, lv_opa_t opa) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = lv_value_precise_t(x1);
  d.p1.y = lv_value_precise_t(y1);
  d.p2.x = lv_value_precise_t(x2);
  d.p2.y = lv_value_precise_t(y2);
  d.color = c(color);
  d.width = width;
  d.opa = opa;
  d.round_start = d.round_end = 1;
  lv_draw_line(layer, &d);
}

void circle(lv_layer_t *layer, float cx, float cy, float r, uint32_t color, lv_opa_t opa) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c(color);
  d.bg_opa = opa;
  d.radius = LV_RADIUS_CIRCLE;
  const lv_area_t a = {int32_t(cx - r + 0.5f), int32_t(cy - r + 0.5f), int32_t(cx + r - 0.5f), int32_t(cy + r - 0.5f)};
  lv_draw_rect(layer, &d, &a);
}

void ring(lv_layer_t *layer, float cx, float cy, float r, int32_t width, uint32_t color, lv_opa_t opa) {
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = c(color);
  d.opa = opa;
  d.width = width;
  d.center.x = int32_t(cx + 0.5f);
  d.center.y = int32_t(cy + 0.5f);
  d.radius = uint16_t(r + width / 2.0f + 0.5f);  // r is the middle of the stroke
  d.start_angle = 0;
  d.end_angle = 360;
  lv_draw_arc(layer, &d);
}

void polygon(lv_layer_t *layer, const float (*p)[2], int n, uint32_t color, lv_opa_t opa) {
  for (int k = 1; k + 1 < n; k++) {
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    const float *v[3] = {p[0], p[k], p[k + 1]};
    for (int i = 0; i < 3; i++) {
      t.p[i].x = lv_value_precise_t(v[i][0]);
      t.p[i].y = lv_value_precise_t(v[i][1]);
    }
    t.color = c(color);
    t.opa = opa;
    lv_draw_triangle(layer, &t);
  }
}

}  // namespace ui::draw
