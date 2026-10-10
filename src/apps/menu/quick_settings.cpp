#include "quick_settings.h"

#include <time.h>

#include "apps/app_manager.h"
#include "board.h"
#include "config.h"
#include "core/net.h"
#include "core/pmu.h"
#include "core/power.h"
#include "core/rtc_clock.h"
#include "core/settings.h"
#include "core/sound.h"
#include "ui/theme.h"

namespace apps {

static constexpr int32_t kHeight = 312;   // visible part of the shade
static constexpr int32_t kOverhang = 28;  // hidden above the screen: square top, round bottom
static constexpr int32_t kRowH = 64, kRowW = LCD_WIDTH - 2 * ui::kPad;
static constexpr int32_t kRowWifi = 72, kRowBright = 148, kRowVolume = 224;  // below the overhang
static constexpr int32_t kMuteW = 64, kVolW = kRowW - kMuteW - 12;           // volume: mute + bar
static constexpr int32_t kTapSlop = 8;                                       // px of movement still counted as a tap
static constexpr lv_opa_t kScrimOpa = 160;                                   // the menu behind, fully pulled
static constexpr uint32_t kFill = 0x2F5F96;                                  // brightness fill: info blue, darkened

static lv_point_t touchPoint() {
  lv_point_t p{0, 0};
  if (lv_indev_t *in = lv_indev_active()) lv_indev_get_point(in, &p);
  return p;
}

// A rounded row, as the Wi-Fi row and the bars use.
static lv_obj_t *row(lv_obj_t *parent, int32_t x, int32_t y, int32_t w) {
  lv_obj_t *r = ui::box(parent);
  lv_obj_set_size(r, w, kRowH);
  lv_obj_set_pos(r, x, kOverhang + y);
  lv_obj_set_style_radius(r, 20, 0);
  lv_obj_set_style_bg_color(r, ui::c(ui::col::surfaceHi), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_clickable(r, true);
  return r;
}

// A bar's fill and value label; the bar clips the fill to its round corners.
static lv_obj_t *barFill(lv_obj_t *bar) {
  lv_obj_set_style_clip_corner(bar, true, 0);
  lv_obj_t *fill = ui::box(bar);
  lv_obj_set_height(fill, kRowH);
  lv_obj_set_style_bg_color(fill, ui::c(kFill), 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  return fill;
}

// Where the finger is along `bar`, as a value in lo..hi.
static int barValue(lv_obj_t *bar, int lo, int hi) {
  lv_area_t a;
  lv_obj_get_coords(bar, &a);
  const int32_t w = lv_area_get_width(&a);
  int32_t x = touchPoint().x - a.x1;
  x = x < 0 ? 0 : x > w ? w : x;
  return lo + (x * (hi - lo) + w / 2) / w;
}

static void setFill(lv_obj_t *fill, int32_t barW, int value, int lo, int hi) {
  const int32_t w = 40 + (barW - 40) * (value - lo) / (hi - lo);  // never empty
  if (lv_obj_get_width(fill) != w) lv_obj_set_width(fill, w);
}

void QuickSettings::create(lv_obj_t *root, lv_obj_t *handle, App *wifiApp) {
  using namespace ui;
  wifiApp_ = wifiApp;

  lv_obj_set_clickable(handle, true);
  lv_obj_add_event_cb(handle, onHandle, LV_EVENT_ALL, this);

  // Dims the menu and closes the shade when tapped. Hidden while closed so it
  // doesn't catch the menu's taps.
  scrim_ = box(root);
  lv_obj_set_size(scrim_, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(scrim_, c(col::bg), 0);
  lv_obj_set_clickable(scrim_, true);
  lv_obj_add_event_cb(scrim_, onScrim, LV_EVENT_CLICKED, this);

  shade_ = box(root);
  lv_obj_set_size(shade_, LCD_WIDTH, kHeight + kOverhang);
  lv_obj_set_style_bg_color(shade_, c(col::surface), 0);
  lv_obj_set_style_bg_opa(shade_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(shade_, kOverhang, 0);
  lv_obj_set_clickable(shade_, true);  // drag it back up
  lv_obj_add_event_cb(shade_, onShade, LV_EVENT_ALL, this);

  // Header: the shade covers the status bar, so it repeats time and battery.
  time_ = label(shade_, &font_title, col::text, "");
  lv_obj_set_pos(time_, kEdge, kOverhang + 18);
  battery_ = label(shade_, &font_caption, col::textDim, "");
  lv_obj_align(battery_, LV_ALIGN_TOP_RIGHT, -kEdge, kOverhang + 28);

  // Wi-Fi: icon on the left, the network's name on the right.
  lv_obj_t *wifi = row(shade_, kPad, kRowWifi, kRowW);
  lv_obj_set_style_pad_hor(wifi, 18, 0);
  pressable(wifi);
  lv_obj_add_event_cb(wifi, onWifi, LV_EVENT_CLICKED, this);
  wifiIcon_ = label(wifi, &font_icon_28, col::text, ICON_WIFI);
  lv_obj_align(wifiIcon_, LV_ALIGN_LEFT_MID, 0, 0);
  wifiName_ = label(wifi, &font_body_medium, col::text, "");
  lv_label_set_long_mode(wifiName_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_max_width(wifiName_, kRowW - 36 - 70, 0);
  lv_obj_align(wifiName_, LV_ALIGN_RIGHT_MID, 0, 0);

  // Brightness: one big bar, filled to the current level.
  bar_ = row(shade_, kPad, kRowBright, kRowW);
  lv_obj_add_event_cb(bar_, onBrightness, LV_EVENT_ALL, this);
  fill_ = barFill(bar_);
  lv_obj_align(label(bar_, &font_icon_28, col::text, ICON_LIGHT_MODE), LV_ALIGN_LEFT_MID, 18, 0);
  pct_ = label(bar_, &font_body_medium, col::text, "");
  lv_obj_align(pct_, LV_ALIGN_RIGHT_MID, -18, 0);

  // Volume: a mute button, then a bar like brightness.
  mute_ = row(shade_, kPad, kRowVolume, kMuteW);
  pressable(mute_);
  lv_obj_add_event_cb(mute_, onMute, LV_EVENT_CLICKED, this);
  muteIcon_ = label(mute_, &font_icon_28, col::text, ICON_VOLUME_UP);
  lv_obj_center(muteIcon_);
  volBar_ = row(shade_, kPad + kMuteW + 12, kRowVolume, kVolW);
  lv_obj_add_event_cb(volBar_, onVolume, LV_EVENT_ALL, this);
  volFill_ = barFill(volBar_);
  volPct_ = label(volBar_, &font_body_medium, col::text, "");
  lv_obj_align(volPct_, LV_ALIGN_RIGHT_MID, -18, 0);

  lv_obj_t *grip = box(shade_);  // drag handle
  lv_obj_set_size(grip, 40, 4);
  lv_obj_align(grip, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_radius(grip, 2, 0);
  lv_obj_set_style_bg_color(grip, c(col::outline), 0);
  lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);

  setY(-kHeight);
  refresh();
}

void QuickSettings::setY(int32_t y) {
  y_ = y < -kHeight ? -kHeight : y > 0 ? 0 : y;
  lv_obj_set_y(shade_, y_ - kOverhang);
  const bool shown = y_ > -kHeight;
  lv_obj_set_hidden(shade_, !shown);
  lv_obj_set_hidden(scrim_, !shown);
  lv_obj_set_style_bg_opa(scrim_, lv_opa_t(kScrimOpa * (y_ + kHeight) / kHeight), 0);
}

void QuickSettings::animateTo(int32_t y) {
  open_ = y == 0;
  if (open_) refresh();
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, this);
  lv_anim_set_values(&a, y_, y);
  lv_anim_set_duration(&a, ui::motion::normal);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *self, int32_t v) { static_cast<QuickSettings *>(self)->setY(v); });
  lv_anim_start(&a);
}

void QuickSettings::open() { animateTo(0); }
void QuickSettings::close() { animateTo(-kHeight); }

void QuickSettings::refresh() {
  if (!shade_) return;
  if (core::clock::valid()) {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    ui::setTextFmt(time_, "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    ui::setText(time_, "--:--");
  }
  const int pct = core::pmu::batteryPercent();
  if (pct >= 0)
    ui::setTextFmt(battery_, core::pmu::onCharger() ? "%d %% · charging" : "%d %%", pct);
  else
    ui::setText(battery_, core::pmu::onCharger() ? "USB power" : "");

  const char *ssid = core::net::ssid();
  const bool up = core::net::connected();
  ui::setText(wifiIcon_, ssid[0] ? ICON_WIFI : ICON_WIFI_OFF);
  lv_obj_set_style_text_color(wifiIcon_, ui::c(up ? ui::col::info : ui::col::textDim), 0);
  ui::setText(wifiName_, ssid[0] ? ssid : "Set up Wi-Fi");
  lv_obj_set_style_text_color(wifiName_, ui::c(ssid[0] ? ui::col::text : ui::col::textDim), 0);
  renderBrightness();
  renderVolume();
}

void QuickSettings::renderBrightness() {
  const int pct = core::settings::brightnessPct();
  setFill(fill_, kRowW, pct, BRIGHTNESS_USER_MIN_PCT, BRIGHTNESS_USER_MAX_PCT);
  ui::setTextFmt(pct_, "%d %%", pct);
}

void QuickSettings::renderVolume() {
  using namespace ui;
  const bool muted = core::settings::muted();
  setFill(volFill_, kVolW, core::settings::volumePct(), 0, 100);
  lv_obj_set_style_bg_color(volFill_, c(muted ? col::outline : kFill), 0);
  if (muted)
    setText(volPct_, "Muted");
  else
    setTextFmt(volPct_, "%d %%", core::settings::volumePct());
  lv_obj_set_style_text_color(volPct_, c(muted ? col::textDim : col::text), 0);
  setText(muteIcon_, muted ? ICON_VOLUME_OFF : ICON_VOLUME_UP);
  lv_obj_set_style_text_color(muteIcon_, c(muted ? col::error : col::text), 0);
  lv_obj_set_style_bg_color(mute_, c(muted ? mixBlack(col::error, 22) : col::surfaceHi), 0);
}

// The status bar: drag down to pull the shade with the finger, or tap it.
void QuickSettings::onHandle(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (self->open_) return;
  if (code == LV_EVENT_PRESSED) {
    self->pressY_ = touchPoint().y;
    self->dragged_ = false;
    lv_anim_delete(self, nullptr);
  } else if (code == LV_EVENT_PRESSING) {
    const int32_t d = touchPoint().y - self->pressY_;
    if (d > kTapSlop) self->dragged_ = true;
    if (self->dragged_) self->setY(-kHeight + d);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (!self->dragged_ || self->y_ > -kHeight * 2 / 3)
      self->open();  // a tap, or pulled a third of the way
    else
      self->close();
  }
}

// The open shade: drag it back up (or let go early and it stays open).
void QuickSettings::onShade(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    self->pressY_ = touchPoint().y;
    self->startY_ = self->y_;
    self->dragged_ = false;
    lv_anim_delete(self, nullptr);
  } else if (code == LV_EVENT_PRESSING) {
    const int32_t d = self->pressY_ - touchPoint().y;
    if (d > kTapSlop) self->dragged_ = true;
    if (self->dragged_) self->setY(self->startY_ - d);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (self->dragged_ && self->y_ < -kHeight / 3)
      self->close();
    else
      self->open();
  }
}

void QuickSettings::onScrim(lv_event_t *e) { static_cast<QuickSettings *>(lv_event_get_user_data(e))->close(); }

void QuickSettings::onWifi(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  lv_anim_delete(self, nullptr);
  self->open_ = false;
  self->setY(-kHeight);  // closed when we come back to the menu
  manager::open(self->wifiApp_);
}

// Live while the finger is down; saved once, when it lifts.
void QuickSettings::onBrightness(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    const int pct = barValue(self->bar_, BRIGHTNESS_USER_MIN_PCT, BRIGHTNESS_USER_MAX_PCT);
    if (pct == core::settings::brightnessPct()) return;
    core::settings::setBrightnessPct(uint8_t(pct));
    core::power::applyBrightness();
    self->renderBrightness();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    core::settings::saveBrightness();
  }
}

// Same as brightness; touching the bar also unmutes. On release a blip
// plays at the new level, unless noise is already playing to judge it by.
void QuickSettings::onVolume(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    const int pct = barValue(self->volBar_, 0, 100);
    if (pct == core::settings::volumePct() && !core::settings::muted()) return;
    core::settings::setVolumePct(uint8_t(pct));
    core::sound::applyVolume();
    self->renderVolume();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    core::settings::saveVolume();
    if (!core::sound::noisePlaying()) core::sound::play(core::sound::Pattern::Confirm);
  }
}

void QuickSettings::onMute(lv_event_t *e) {
  auto *self = static_cast<QuickSettings *>(lv_event_get_user_data(e));
  core::settings::setMuted(!core::settings::muted());
  core::sound::applyVolume();
  self->renderVolume();
}

}  // namespace apps
