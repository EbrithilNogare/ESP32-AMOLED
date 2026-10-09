#pragma once
#include <lvgl.h>
#include <stdint.h>

#include "generated/assets.h"

// Design tokens from docs/UI_DESIGN.md, implemented once. Apps use these
// instead of hard-coding colours, sizes or durations.
namespace ui {

namespace col {
constexpr uint32_t bg = 0x000000;
constexpr uint32_t surface = 0x141414;
constexpr uint32_t surfaceHi = 0x242424;
constexpr uint32_t track = 0x1E1E1E;
constexpr uint32_t outline = 0x3A3A3A;
constexpr uint32_t text = 0xEDEDED;
constexpr uint32_t textDim = 0x9A9A9A;
constexpr uint32_t textOff = 0x5A5A5A;
constexpr uint32_t success = 0x4CD07D;
constexpr uint32_t warning = 0xFFB020;
constexpr uint32_t error = 0xFF5A5F;
constexpr uint32_t info = 0x4DA3FF;
}  // namespace col

namespace motion {
constexpr uint32_t fast = 120;
constexpr uint32_t normal = 220;
constexpr uint32_t slow = 400;
}  // namespace motion

constexpr int32_t kMargin = 4;  // pixel-shift travel; nothing touches the edge
constexpr int32_t kPad = 16;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }
lv_color_t mixBlack(uint32_t hex, uint8_t percent);  // accent at N % on black

void begin();
lv_obj_t *screen();               // a new black screen
lv_obj_t *box(lv_obj_t *parent);  // plain container: no bg, padding or scrolling
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *pill(lv_obj_t *parent, const char *icon, const char *text, uint32_t color);  // 56 px
lv_obj_t *roundButton(lv_obj_t *parent, const char *icon, uint32_t color);             // 56 px
void setPillContent(lv_obj_t *pill, const char *icon, const char *text);
void pressable(lv_obj_t *obj);  // shrink + brighten while pressed

// lv_label_set_text() always invalidates; these skip unchanged text so
// once-a-second refreshes don't redraw (and flush) an identical label.
void setText(lv_obj_t *label, const char *text);
void setTextFmt(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace ui
