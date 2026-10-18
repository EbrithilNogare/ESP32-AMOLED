#include "ha_app.h"

#include <Arduino.h>

#include "board.h"
#include "config.h"
#include "core/net.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

using ha::LightState;
using ha::Result;

static const AppInfo kInfo = {"home_assistant", "Lights", ICON_LIGHTBULB, 0xFFB300};
static constexpr uint32_t kOnText = 0x1E1500;  // on the accent fill
static constexpr int32_t kGap = 8;
static constexpr int32_t kTileW = (LCD_WIDTH - 2 * ui::kMargin - kGap) / 2;
static constexpr int32_t kTileH = (LCD_HEIGHT - 2 * ui::kMargin - kGap) / 2;  // up to four: a 2x2 grid
static constexpr int32_t kTileHSmall = 148;                                   // more: a scrolling grid
static_assert(sizeof(core::store::RtcState::haStates) >= ha_client::kMaxLights, "RtcState::haStates too small");

// The light list, kept in NVS so the tiles are there at once next time.
struct ListCache {
  uint8_t n;
  ha::LightInfo lights[ha_client::kMaxLights];
};

const AppInfo &HomeAssistantApp::info() const { return kInfo; }

void HomeAssistantApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  root_ = root;
  if (!clientStarted_) {
    ha_client::begin();
    clientStarted_ = true;
    static ListCache cache;  // ~1.3 KB: not on the stack
    if (core::store::load("halights", &cache, sizeof cache)) {
      ha_client::setList(cache.lights, cache.n);
      // States from before deep sleep (same list: it's saved only when it changes).
      if (core::store::rtcValid())
        for (int i = 0; i < ha_client::count(); i++) tiles_[i].value = LightState(core::store::rtc().haStates[i]);
    }
  }
  listLoaded_ = false;
  problem_ = Result::Ok;
  for (Tile &t : tiles_) t.stale = true;  // until Home Assistant answers

  empty_ = label(root, &font_body, col::textDim, "");
  lv_obj_set_width(empty_, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(empty_, LV_ALIGN_CENTER, 0, 0);
  buildTiles();

  banner_ = toast(root, "");
  lv_obj_align(banner_, LV_ALIGN_BOTTOM_MID, 0, -kBarBottom);

  open_ = true;
  core::net::acquire();
  const uint32_t now = millis();
  nextListMs_ = now;               // the list (with every state) first
  nextPollMs_ = now + HA_POLL_MS;  // then the states
  retryMs_ = HA_POLL_MS;
  updateBanner();
}

void HomeAssistantApp::onDestroy() {
  open_ = false;
  core::net::release();
  for (Tile &t : tiles_) t.obj = t.icon = t.name = t.state = t.spinner = nullptr;
  root_ = grid_ = banner_ = empty_ = nullptr;
  nTiles_ = 0;
}

void HomeAssistantApp::onSleep(sleepplan::Kind) {
  for (int i = 0; i < ha_client::kMaxLights; i++) core::store::rtc().haStates[i] = uint8_t(tiles_[i].value);
  core::store::rtcCommit();
}

void HomeAssistantApp::onTick(uint32_t nowMs) {
  if (!open_ || !core::net::connected() || ha_client::busy()) return;
  if (core::reached(nowMs, nextListMs_)) {
    if (ha_client::loadLights()) nextListMs_ = nowMs + HA_LIST_MS;
  } else if (nTiles_ && core::reached(nowMs, nextPollMs_)) {
    if (ha_client::refresh()) nextPollMs_ = nowMs + retryMs_;
  }
}

void HomeAssistantApp::onWifiChanged() {
  if (!open_) return;
  const uint32_t now = millis();
  if (!listLoaded_) nextListMs_ = now;  // (re)connected: ask right away
  nextPollMs_ = now;
  retryMs_ = HA_POLL_MS;
  updateBanner();
}

// Up to four lights fill a 2x2 grid; more scroll in two columns.
void HomeAssistantApp::buildTiles() {
  using namespace ui;
  if (grid_) lv_obj_delete(grid_);
  nTiles_ = ha_client::count();
  const bool scroll = nTiles_ > 4;
  grid_ = scroll ? lv_obj_create(root_) : box(root_);  // a plain lv_obj: it scrolls, so it stays clickable
  lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid_, 0, 0);
  lv_obj_set_style_pad_all(grid_, kMargin, 0);
  lv_obj_set_style_pad_gap(grid_, kGap, 0);
  lv_obj_set_size(grid_, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_move_to_index(grid_, 0);  // under the banner
  if (scroll) {
    lv_obj_set_scroll_dir(grid_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(grid_, kBarBottom + 40, 0);  // the last row clears the banner
  }

  for (int i = 0; i < nTiles_; i++) {
    Tile &t = tiles_[i];
    t.pending = false;
    t.obj = lv_button_create(grid_);
    lv_obj_set_size(t.obj, kTileW, scroll ? kTileHSmall : kTileH);
    lv_obj_set_style_radius(t.obj, 24, 0);
    lv_obj_set_style_pad_top(t.obj, scroll ? 14 : 20, 0);
    lv_obj_set_style_pad_hor(t.obj, 18, 0);
    lv_obj_set_style_pad_bottom(t.obj, scroll ? 14 : 18, 0);
    lv_obj_set_style_shadow_width(t.obj, 0, 0);
    lv_obj_set_style_border_width(t.obj, 0, 0);
    lv_obj_set_style_border_color(t.obj, c(col::outline), 0);
    lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(t.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t.obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_on_focus(t.obj, false);
    pressable(t.obj);
    lv_obj_add_event_cb(t.obj, onTileClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(t.obj, reinterpret_cast<void *>(intptr_t(i)));

    t.icon = label(t.obj, scroll ? &font_icon_28 : &font_icon_56, col::text, ICON_LIGHTBULB);
    t.name = label(t.obj, &font_title, col::text, ha_client::light(i).name);
    lv_obj_set_width(t.name, kTileW - 36);
    lv_label_set_long_mode(t.name, scroll ? LV_LABEL_LONG_MODE_DOTS : LV_LABEL_LONG_MODE_WRAP);
    if (scroll) lv_obj_set_height(t.name, lv_font_get_line_height(&font_title));  // one line, "…" if longer

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
  for (int i = nTiles_; i < ha_client::kMaxLights; i++) tiles_[i] = Tile{};
  updateBanner();
}

void HomeAssistantApp::renderTile(int i) {
  Tile &t = tiles_[i];
  if (!t.obj) return;
  const bool unavailable = t.value == LightState::Unavailable || t.value == LightState::Unknown;
  const bool lit = t.value == LightState::On;
  uint32_t fill = ui::col::surface, text = ui::col::text, iconColor = ui::col::textOff;
  if (lit) {
    fill = kInfo.accent;
    text = iconColor = kOnText;
  } else if (unavailable && !t.pending) {
    fill = ui::col::bg;
    text = iconColor = ui::col::textOff;
  }
  lv_obj_set_style_bg_color(t.obj, ui::c(fill), 0);
  lv_obj_set_style_text_color(t.name, ui::c(text), 0);
  lv_obj_set_style_border_width(t.obj, unavailable && !t.pending ? 2 : 0, 0);
  lv_obj_set_style_text_color(t.icon, ui::c(iconColor), 0);
  lv_obj_set_style_text_color(t.state, ui::c(text), 0);
  lv_obj_set_style_arc_color(t.spinner, ui::c(text), LV_PART_INDICATOR);

  const char *s = t.value == LightState::On ? "ON" : t.value == LightState::Off ? "OFF" : "N/A";
  if (t.value == LightState::Unknown && t.stale)
    ui::setText(t.state, "…");  // not fetched yet
  else if (t.stale && !unavailable)
    ui::setTextFmt(t.state, "%s …", s);
  else
    ui::setText(t.state, s);
  lv_obj_set_hidden(t.spinner, !t.pending);
}

// Keeps what the banner should explain, and slows polling down while Home
// Assistant can't be reached (up to HA_RETRY_MAX_MS).
void HomeAssistantApp::noteResult(Result r, uint32_t nowMs) {
  const bool unreachable = r == Result::NetworkError || r == Result::HostNotFound || r == Result::ServerError;
  if (r == Result::Unauthorized)
    problem_ = r;
  else if (unreachable && problem_ != Result::Unauthorized)
    problem_ = r;  // network errors say nothing about the token
  else if (r == Result::Ok || r == Result::NotFound)
    problem_ = Result::Ok;
  if (unreachable) {
    retryMs_ = retryMs_ * 2 > HA_RETRY_MAX_MS ? HA_RETRY_MAX_MS : retryMs_ * 2;
    nextPollMs_ = nowMs + retryMs_;
  } else {
    retryMs_ = HA_POLL_MS;
  }
}

void HomeAssistantApp::onLights(Result result) {
  const bool changed = ha_client::adopt(result);
  if (!open_) {
    if (changed)
      for (Tile &t : tiles_) t.value = LightState::Unknown;  // states were by position in the old list
    return;
  }
  const uint32_t now = millis();
  noteResult(result, now);
  if (result != Result::Ok) {
    if (!listLoaded_) nextListMs_ = now + retryMs_;  // never loaded: retry with the back-off
    updateBanner();
    return;
  }
  listLoaded_ = true;
  if (changed) {
    static ListCache cache;
    cache.n = uint8_t(ha_client::count());
    for (int i = 0; i < cache.n; i++) cache.lights[i] = ha_client::light(i);
    core::store::save("halights", &cache, sizeof cache);
  }
  // The list carries every state: fresh tiles, except where a tap is pending.
  for (int i = 0; i < ha_client::count(); i++) {
    if (!changed && tiles_[i].pending) continue;
    tiles_[i].value = ha_client::light(i).state;
    tiles_[i].stale = false;
  }
  if (changed)
    buildTiles();
  else
    for (int i = 0; i < nTiles_; i++) renderTile(i);
  nextPollMs_ = now + HA_POLL_MS;
  updateBanner();
}

void HomeAssistantApp::onResult(int i, LightState state, Result result, bool toggle, uint8_t gen) {
  if (i < 0 || i >= nTiles_ || gen != ha_client::generation()) return;  // for a list since replaced
  Tile &t = tiles_[i];
  noteResult(result, millis());
  // A poll queued before the tap would show the old state and end the
  // spinner early; the toggle's own result follows.
  if (t.pending && !toggle) return;
  const bool wasPending = t.pending;
  t.pending = false;
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

void HomeAssistantApp::updateBanner() {
  if (!banner_) return;
  const char *text = problem_ == Result::Unauthorized   ? "HA token rejected — check secrets.h"
                     : !core::net::connected()          ? "Connecting…"
                     : problem_ == Result::HostNotFound ? "Home Assistant not found"
                     : problem_ != Result::Ok           ? "Can't reach Home Assistant"
                                                        : nullptr;
  lv_obj_set_hidden(banner_, !text);
  if (text) {
    ui::setText(banner_, text);
    lv_obj_set_style_text_color(banner_, ui::c(problem_ == Result::Unauthorized ? ui::col::error : ui::col::textDim),
                                0);
  }
  lv_obj_set_hidden(empty_, nTiles_ > 0);
  ui::setText(empty_, listLoaded_ ? "No lights in Home Assistant" : "Loading lights…");
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
  const int i = ui::eventIndex(e);
  Tile &t = self->tiles_[i];
  if (t.pending) return;
  const bool known = t.value == LightState::On || t.value == LightState::Off;
  if (!known || !core::net::connected() || !ha_client::toggle(i)) {
    shake(obj);
    return;
  }
  t.pending = true;
  self->renderTile(i);
}

}  // namespace apps
