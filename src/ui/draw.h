#pragma once
#include <lvgl.h>
#include <stdint.h>

// Immediate-mode drawing for apps that render in an object's
// LV_EVENT_DRAW_MAIN (Dice, Level): thin wrappers over lv_draw_* in screen
// coordinates. Text is copied by LVGL, so temporary buffers are fine.
namespace ui::draw {

void text(lv_layer_t *layer, const lv_font_t *font, uint32_t color, lv_opa_t opa, const char *txt, int32_t cx,
          int32_t cy, int32_t letterSpace = 0, lv_text_align_t align = LV_TEXT_ALIGN_CENTER);
// Round ends cost two anti-aliased circles each; pass round = false where
// many short lines meet anyway (wireframes).
void line(lv_layer_t *layer, float x1, float y1, float x2, float y2, uint32_t color, int32_t width, lv_opa_t opa,
          bool round = true);
void circle(lv_layer_t *layer, float cx, float cy, float r, uint32_t color, lv_opa_t opa);  // filled
void ring(lv_layer_t *layer, float cx, float cy, float r, int32_t width, uint32_t color, lv_opa_t opa);
// Convex polygon, filled as a triangle fan from the first point.
void polygon(lv_layer_t *layer, const float (*pts)[2], int n, uint32_t color, lv_opa_t opa);

}  // namespace ui::draw
