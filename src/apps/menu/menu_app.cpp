#include "menu_app.h"

#include <Arduino.h>
#include <time.h>

#include "apps/app_manager.h"
#include "board.h"
#include "core/net.h"
#include "core/pmu.h"
#include "core/rtc_clock.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

static constexpr int32_t kRowH = 176;
static constexpr int32_t kDisc = 128;

static void applyFocus(lv_obj_t *list);

static const AppInfo kInfo = {"menu", "Menu", ICON_SCHEDULE, ui::col::text};
const AppInfo &MenuApp::info() const { return kInfo; }

static const char *batteryIcon(int pct, bool charging) {
  if (charging) return ICON_BOLT;
  if (pct < 0) return ICON_BATTERY_ALERT;
  if (pct >= 95) return ICON_BATTERY_FULL;
  if (pct >= 80) return ICON_BATTERY_6_BAR;
  if (pct >= 65) return ICON_BATTERY_5_BAR;
  if (pct >= 50) return ICON_BATTERY_4_BAR;
  if (pct >= 35) return ICON_BATTERY_3_BAR;
  if (pct >= 20) return ICON_BATTERY_2_BAR;
  if (pct >= 10) return ICON_BATTERY_1_BAR;
  return ICON_BATTERY_0_BAR;
}

void MenuApp::onCreate(lv_obj_t *root) {
  using namespace ui;

  // Status bar: time on the left, Wi-Fi + battery on the right. Pulling it
  // down opens the quick settings (created last, so they cover the list).
  lv_obj_t *bar = box(root);
  lv_obj_set_size(bar, LCD_WIDTH - 2 * kMargin, 32);
  lv_obj_set_pos(bar, kMargin, kMargin);
  lv_obj_set_style_pad_hor(bar, kPad, 0);
  time_ = label(bar, &font_caption, col::textDim, "");
  lv_obj_align(time_, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *right = box(bar);
  lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right, 6, 0);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
  wifi_ = label(right, &font_icon_18, col::textDim, ICON_WIFI);
  batIcon_ = label(right, &font_icon_18, col::textDim, "");
  batText_ = label(right, &font_caption, col::textDim, "");

  // Scrolling list of rows, two apps per row. Not a ui::box: it has to stay
  // clickable so a drag that starts between the discs still scrolls it.
  list_ = lv_obj_create(root);
  lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_, 0, 0);
  lv_obj_set_style_pad_all(list_, 0, 0);
  lv_obj_set_style_pad_top(list_, 8, 0);
  lv_obj_set_style_pad_bottom(list_, 140, 0);  // lets the last row reach the centre
  lv_obj_set_size(list_, LCD_WIDTH, LCD_HEIGHT - 36);
  lv_obj_set_pos(list_, 0, 36);
  lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(list_, LV_SCROLL_SNAP_START);
  lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(list_, onScroll, LV_EVENT_SCROLL, nullptr);

  lv_obj_t *row = nullptr;
  for (size_t i = 0; i < manager::count(); i++) {
    App *app = manager::at(i);
    const AppInfo &ai = app->info();
    if (i % 2 == 0) {
      row = box(list_);
      lv_obj_set_size(row, LCD_WIDTH, kRowH);
      lv_obj_set_snappable(row, true);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_set_style_pad_column(row, 40, 0);
    }
    lv_obj_t *cell = box(row);
    lv_obj_set_size(cell, kDisc, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 10, 0);
    lv_obj_set_scroll_on_focus(cell, false);

    lv_obj_t *disc = box(cell);
    lv_obj_set_size(disc, kDisc, kDisc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, c(mixBlack(ai.accent, 15)), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(disc, c(ai.accent), 0);
    lv_obj_set_style_border_width(disc, 2, 0);
    pressable(disc);
    lv_obj_set_style_bg_color(disc, c(mixBlack(ai.accent, 30)), LV_STATE_PRESSED);
    lv_obj_set_clickable(disc, true);
    lv_obj_add_event_cb(disc, onAppClicked, LV_EVENT_CLICKED, app);
    lv_obj_t *icon = label(disc, &font_icon_64, ai.accent, ai.icon);
    lv_obj_center(icon);

    label(cell, &font_body, col::textDim, ai.name);
  }

  quick_.create(root, bar, wifiSettings_);
  nextStatusMs_ = millis();
  lv_obj_update_layout(list_);
  applyFocus(list_);
}

void MenuApp::updateStatus() {
  if (!time_) return;
  if (core::clock::valid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    ui::setTextFmt(time_, "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    ui::setText(time_, "--:--");
  }
  lv_obj_set_hidden(wifi_, !core::net::connected());
  const int pct = core::pmu::batteryPercent();
  ui::setText(batIcon_, batteryIcon(pct, core::pmu::onCharger()));
  if (pct >= 0)
    ui::setTextFmt(batText_, "%d %%", pct);
  else
    ui::setText(batText_, "");
}

void MenuApp::onTick(uint32_t nowMs) {
  if (core::reached(nowMs, nextStatusMs_)) {
    nextStatusMs_ = nowMs + 1000;
    updateStatus();
    if (quick_.isOpen()) quick_.refresh();
  }
}

bool MenuApp::onBack() {
  if (quick_.isOpen()) {
    quick_.close();
    return true;
  }
  if (list_) lv_obj_scroll_to_y(list_, 0, LV_ANIM_ON);
  return true;
}

void MenuApp::onAppClicked(lv_event_t *e) { manager::open(static_cast<App *>(lv_event_get_user_data(e))); }

// Rows away from the list's centre fade slightly (watch-style focus). Done
// with colour opacity on each element: object opacity would need a layer.
static void applyFocus(lv_obj_t *list) {
  const int32_t centre = lv_obj_get_scroll_y(list) + lv_obj_get_height(list) / 2 - 30;
  const uint32_t rows = lv_obj_get_child_count(list);
  for (uint32_t r = 0; r < rows; r++) {
    lv_obj_t *row = lv_obj_get_child(list, r);
    int32_t d = lv_obj_get_y(row) + kRowH / 2 - centre;
    if (d < 0) d = -d;
    const int32_t k = d > 240 ? 240 : d;
    const lv_opa_t opa = lv_opa_t(255 - k * 100 / 240);  // 100 % → ~60 %
    // Style setters redraw even when nothing changes; most rows don't.
    lv_obj_t *first = lv_obj_get_child(lv_obj_get_child(row, 0), 0);
    if (lv_obj_get_style_bg_opa(first, LV_PART_MAIN) == opa) continue;
    for (uint32_t i = 0; i < lv_obj_get_child_count(row); i++) {
      lv_obj_t *cell = lv_obj_get_child(row, i);
      lv_obj_t *disc = lv_obj_get_child(cell, 0);
      lv_obj_set_style_bg_opa(disc, opa, 0);
      lv_obj_set_style_border_opa(disc, opa, 0);
      lv_obj_set_style_text_opa(lv_obj_get_child(disc, 0), opa, 0);
      lv_obj_set_style_text_opa(lv_obj_get_child(cell, 1), opa, 0);
    }
  }
}

void MenuApp::onScroll(lv_event_t *e) { applyFocus(static_cast<lv_obj_t *>(lv_event_get_target(e))); }

}  // namespace apps
