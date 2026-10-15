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

// Layout grid (UI_DESIGN §4).
constexpr int32_t kMargin = 4;  // pixel-shift travel; nothing touches the edge
constexpr int32_t kPad = 16;
constexpr int32_t kEdge = 24;       // headers and bottom bars, from the screen edge
constexpr int32_t kBarBottom = 20;  // bottom bar, from the bottom edge
constexpr int32_t kBtn = 56;        // button height
constexpr int32_t kBtnSmall = 48;   // bottom bars with three controls

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }
inline uint32_t hex(lv_color_t color) { return lv_color_to_u32(color) & 0xFFFFFF; }
uint32_t mixBlack(uint32_t hex, uint8_t percent);  // accent at N % on black

void begin();
lv_obj_t *screen();               // a new black screen
lv_obj_t *box(lv_obj_t *parent);  // plain container: no bg, padding, scrolling or clicks
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *header(lv_obj_t *parent, const char *text, uint32_t color);  // caption, top left

// Buttons: rounded, `surface` fill, content in `color`, press feedback.
lv_obj_t *pill(lv_obj_t *parent, const char *icon, const char *text, uint32_t color);  // kBtn
lv_obj_t *roundButton(lv_obj_t *parent, const char *icon, uint32_t color);             // kBtn
lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t color, int32_t height = kBtn);
void setPillContent(lv_obj_t *pill, const char *icon, const char *text);
void pressable(lv_obj_t *obj);  // shrink + brighten while pressed

// Segmented control: a kBtnSmall pill of `n` tappable segments. `cb` gets
// the segment as current target with its index in user data.
lv_obj_t *segmented(lv_obj_t *parent, const char *const *labels, int n, const lv_font_t *font, int32_t segWidth,
                    lv_event_cb_t cb, void *user);
void setSegment(lv_obj_t *seg, int selected, lv_color_t fill, lv_color_t text);
inline int eventIndex(lv_event_t *e) {
  return int(intptr_t(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_current_target(e)))));
}

lv_obj_t *toast(lv_obj_t *parent, const char *text);  // caption on a raised chip
void flash(lv_obj_t *parent, uint32_t color);         // full-screen pulse (a timer ended); taps go through
// Burn-in drift for minimal views (UI_DESIGN §6): a different offset within
// ±ax, ±ay for every step, e.g. every minute.
lv_point_t drift(int32_t step, int32_t ax, int32_t ay);
lv_obj_t *progressRing(lv_obj_t *parent, int32_t size, int32_t width);  // 0..1000 from 12 o'clock

// lv_label_set_text() always invalidates; these skip unchanged text so
// once-a-second refreshes don't redraw (and flush) an identical label.
void setText(lv_obj_t *label, const char *text);
void setTextFmt(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace ui
