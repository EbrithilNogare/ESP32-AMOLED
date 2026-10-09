#include "ha_app.h"

#include <Arduino.h>

#include "board.h"
#include "config.h"
#include "core/net.h"
#include "core/store.h"
#include "ui/theme.h"

namespace apps {

using ha::LightState;
using ha::Result;

static const AppInfo kInfo = {"home_assistant", "Lights", ICON_LIGHTBULB, 0xFFB300};
static constexpr uint32_t kOnFill = 0xFFB300, kOnText = 0x1E1500;
static constexpr int32_t kGap = 8;
static constexpr int32_t kTileW = (LCD_WIDTH - 2 * ui::kMargin - kGap) / 2;
static constexpr int32_t kTileH = (LCD_HEIGHT - 2 * ui::kMargin - kGap) / 2;

const AppInfo &HomeAssistantApp::info() const { return kInfo; }

void HomeAssistantApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  if (!clientStarted_) {
    ha_client::begin();
    clientStarted_ = true;
  }
  // Last known states from before deep sleep, shown as stale until refreshed.
  if (core::store::rtcValid()) {
    for (int i = 0; i < ha_client::kLights; i++) {
      if (tiles_[i].value == LightState::Unknown) tiles_[i].value = LightState(core::store::rtc().haStates[i]);
    }
  }

  for (int i = 0; i < ha_client::kLights; i++) {
    Tile &t = tiles_[i];
    t.obj = lv_button_create(root);
    lv_obj_set_size(t.obj, kTileW, kTileH);
    lv_obj_set_pos(t.obj, kMargin + (i % 2) * (kTileW + kGap), kMargin + (i / 2) * (kTileH + kGap));
    lv_obj_set_style_radius(t.obj, 24, 0);
    lv_obj_set_style_pad_top(t.obj, 20, 0);
    lv_obj_set_style_pad_hor(t.obj, 18, 0);
    lv_obj_set_style_pad_bottom(t.obj, 18, 0);
    lv_obj_set_style_shadow_width(t.obj, 0, 0);
    lv_obj_set_style_border_width(t.obj, 0, 0);
    lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(t.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t.obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    pressable(t.obj);
    lv_obj_add_event_cb(t.obj, onTileClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(t.obj, reinterpret_cast<void *>(intptr_t(i)));

    t.icon = label(t.obj, &font_icon_56, col::text, ICON_LIGHTBULB);
    t.name = label(t.obj, &font_title, col::text, ha_client::light(i).label);
    lv_obj_set_width(t.name, kTileW - 36);
    lv_label_set_long_mode(t.name, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *row = box(t.obj);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    t.state = label(row, &font_caption, col::text, "");
    lv_obj_set_style_text_letter_space(t.state, 1, 0);
    t.spinner = lv_spinner_create(row);
    lv_spinner_set_anim_params(t.spinner, 700, 270);
    lv_obj_set_size(t.spinner, 16, 16);
    lv_obj_set_style_arc_width(t.spinner, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(t.spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(t.spinner, 2, LV_PART_INDICATOR);
    renderTile(i);
  }

  banner_ = label(root, &font_caption, col::text, "");
  lv_obj_set_style_bg_color(banner_, c(col::surfaceHi), 0);
  lv_obj_set_style_bg_opa(banner_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(banner_, 12, 0);
  lv_obj_set_style_pad_hor(banner_, 14, 0);
  lv_obj_set_style_pad_ver(banner_, 8, 0);
  lv_obj_align(banner_, LV_ALIGN_CENTER, 0, 0);

  open_ = true;
  core::net::acquire();
  nextPollMs_ = 0;
  updateBanner();
}

void HomeAssistantApp::onDestroy() {
  open_ = false;
  core::net::release();
  for (Tile &t : tiles_) t.obj = t.icon = t.name = t.state = t.spinner = nullptr;
  banner_ = nullptr;
}

void HomeAssistantApp::onSleep() {
  for (int i = 0; i < ha_client::kLights; i++) core::store::rtc().haStates[i] = uint8_t(tiles_[i].value);
  core::store::rtcCommit();
}

void HomeAssistantApp::onTick(uint32_t nowMs) {
  if (!open_ || !core::net::connected() || nowMs < nextPollMs_ || ha_client::busy()) return;
  if (ha_client::refreshAll()) nextPollMs_ = nowMs + HA_POLL_MS;
}

void HomeAssistantApp::onWifiChanged() {
  if (!open_) return;
  nextPollMs_ = 0;  // refresh right after (re)connecting
  updateBanner();
}

void HomeAssistantApp::renderTile(int i) {
  Tile &t = tiles_[i];
  if (!t.obj) return;
  const bool unavailable = t.value == LightState::Unavailable || t.value == LightState::Unknown;
  const bool lit = t.value == LightState::On;
  uint32_t fill = ui::col::surface, text = ui::col::text, iconColor = ui::col::textOff;
  if (lit) {
    fill = kOnFill;
    text = iconColor = kOnText;
  } else if (unavailable && !t.pending) {
    fill = ui::col::bg;
    text = iconColor = ui::col::textOff;
  }
  lv_obj_set_style_bg_color(t.obj, ui::c(fill), 0);
  lv_obj_set_style_text_color(t.name, ui::c(text), 0);
  lv_obj_set_style_border_color(t.obj, ui::c(ui::col::outline), 0);
  lv_obj_set_style_border_width(t.obj, unavailable && !t.pending ? 2 : 0, 0);
  lv_obj_set_style_text_color(t.icon, ui::c(iconColor), 0);
  lv_obj_set_style_text_color(t.state, ui::c(text), 0);
  lv_obj_set_style_arc_color(t.spinner, ui::c(text), LV_PART_INDICATOR);

  const char *s = t.value == LightState::On ? "ON" : t.value == LightState::Off ? "OFF" : "N/A";
  if (t.value == LightState::Unknown && t.stale)
    lv_label_set_text(t.state, "…");  // not fetched yet
  else if (t.stale && !unavailable)
    lv_label_set_text_fmt(t.state, "%s …", s);
  else
    lv_label_set_text(t.state, s);
  if (t.pending)
    lv_obj_set_hidden(t.spinner, false);
  else
    lv_obj_set_hidden(t.spinner, true);
}

void HomeAssistantApp::onResult(int i, LightState state, Result result) {
  if (i < 0 || i >= ha_client::kLights) return;
  Tile &t = tiles_[i];
  const bool wasPending = t.pending;
  t.pending = false;
  unauthorized_ = result == Result::Unauthorized;
  if (result == Result::Ok) {
    t.value = state;
    t.stale = false;
  } else if (result == Result::NotFound) {
    t.value = LightState::Unavailable;
    t.stale = false;
  } else if (wasPending && t.obj) {
    shake(t.obj);  // toggle failed: the tile keeps its old state
  }
  renderTile(i);
  updateBanner();
}

void HomeAssistantApp::setBanner(const char *text, uint32_t color) {
  if (!banner_) return;
  if (!text) {
    lv_obj_set_hidden(banner_, true);
    return;
  }
  lv_label_set_text(banner_, text);
  lv_obj_set_style_text_color(banner_, ui::c(color), 0);
  lv_obj_set_hidden(banner_, false);
}

void HomeAssistantApp::updateBanner() {
  if (unauthorized_)
    setBanner("HA token rejected — check secrets.h", ui::col::error);
  else if (!core::net::connected())
    setBanner("Connecting…", ui::col::textDim);
  else
    setBanner(nullptr, 0);
}

void HomeAssistantApp::shake(lv_obj_t *obj) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_values(&a, -6, 6);
  lv_anim_set_duration(&a, 60);
  lv_anim_set_playback_duration(&a, 60);
  lv_anim_set_repeat_count(&a, 2);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_obj_set_style_translate_x((lv_obj_t *)an->var, 0, 0); });
  lv_anim_start(&a);
}

void HomeAssistantApp::onTileClicked(lv_event_t *e) {
  auto *self = static_cast<HomeAssistantApp *>(lv_event_get_user_data(e));
  lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  const int i = int(intptr_t(lv_obj_get_user_data(obj)));
  Tile &t = self->tiles_[i];
  if (t.pending) return;
  if (t.value != LightState::On && t.value != LightState::Off) {
    self->shake(obj);
    return;
  }
  if (!core::net::connected() || !ha_client::toggle(i)) {
    self->shake(obj);
    return;
  }
  t.pending = true;
  self->renderTile(i);
}

}  // namespace apps
