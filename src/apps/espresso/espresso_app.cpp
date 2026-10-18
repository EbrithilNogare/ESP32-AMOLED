#include "espresso_app.h"

#include <Arduino.h>

#include <type_traits>

#include "board.h"
#include "config.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

using shot::Mode;
using shot::Zone;

static_assert(std::is_trivially_copyable<shot::ShotTimer>::value, "parked in RTC memory as is");
static const AppInfo kInfo = {"espresso", "Espresso", ICON_COFFEE, 0xC8915A};
static constexpr int32_t kBarX = 34, kBarW = LCD_WIDTH - 2 * kBarX, kBarY = 268;
static constexpr uint32_t kScaleS = 40;    // bar spans 0-40 s
static constexpr uint32_t kFrameMs = 100;  // tenths of a second: 10 fps is enough
static constexpr uint8_t kChips = 4;       // history chips shown

static uint32_t zoneColor(Zone z) {
  switch (z) {
    case Zone::Early: return ui::col::info;
    case Zone::Close: return ui::col::warning;
    case Zone::InTarget: return ui::col::success;
    default: return ui::col::error;
  }
}

EspressoApp::EspressoApp() : timer_({ESPRESSO_TARGET_MIN_S * 10, ESPRESSO_TARGET_MAX_S * 10, 50}) {}

const AppInfo &EspressoApp::info() const { return kInfo; }

power::Policy EspressoApp::powerPolicy() const {
  return timer_.mode() == Mode::Running ? power::Policy::KeepAwake : power::Policy::Normal;
}
uint32_t EspressoApp::idleDimMs() const { return ESPRESSO_IDLE_DIM_MS; }

// A shot still running when the device sleeps ends there, recorded; the
// result is still on screen after the wake (the whole timer is parked).
void EspressoApp::onSleep(sleepplan::Kind) {
  if (timer_.mode() == Mode::Running) {
    timer_.tap(millis());
    saveHistory();
  }
  core::store::park("shot", &timer_, sizeof timer_);
}

void EspressoApp::onWake(Wake wake) {
  if (wake == Wake::Cold || !core::store::unpark("shot", &timer_, sizeof timer_)) loadHistory();  // else it came along
}

void EspressoApp::loadHistory() {
  uint16_t h[shot::kHistory + 1] = {};  // [0] = count
  if (core::store::load("shots", h, sizeof h)) timer_.loadHistory(h + 1, uint8_t(h[0]));
}

void EspressoApp::saveHistory() {
  uint16_t h[shot::kHistory + 1] = {};
  h[0] = timer_.historyCount();
  for (uint8_t i = 0; i < timer_.historyCount(); i++) h[i + 1] = timer_.historyAt(i);
  core::store::save("shots", h, sizeof h);
}

static lv_obj_t *rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius) {
  lv_obj_t *o = ui::box(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void EspressoApp::onCreate(lv_obj_t *root) {
  using namespace ui;

  header(root, "ESPRESSO", col::textDim);
  lv_obj_t *target = header(root, "", col::textDim);
  setTextFmt(target, "TARGET %d–%d S", ESPRESSO_TARGET_MIN_S, ESPRESSO_TARGET_MAX_S);
  lv_obj_align(target, LV_ALIGN_TOP_RIGHT, -kEdge, kEdge);

  digits_ = label(root, &font_digits_120, col::text, "0.0");
  lv_obj_align(digits_, LV_ALIGN_TOP_MID, 0, 112);

  lv_obj_set_style_bg_color(rect(root, kBarX, kBarY, kBarW, 12, 6), c(col::surface), 0);
  const int32_t bandX = kBarX + kBarW * ESPRESSO_TARGET_MIN_S / int32_t(kScaleS);
  const int32_t bandW = kBarW * (ESPRESSO_TARGET_MAX_S - ESPRESSO_TARGET_MIN_S) / int32_t(kScaleS);
  lv_obj_t *band = rect(root, bandX, kBarY - 6, bandW, 24, 4);
  lv_obj_set_style_bg_color(band, c(col::success), 0);
  lv_obj_set_style_bg_opa(band, LV_OPA_20, 0);
  lv_obj_set_style_border_color(band, c(col::success), 0);
  lv_obj_set_style_border_opa(band, LV_OPA_50, 0);
  lv_obj_set_style_border_width(band, 1, 0);
  fill_ = rect(root, kBarX, kBarY, 0, 12, 6);

  const int ticks[] = {0, ESPRESSO_TARGET_MIN_S, ESPRESSO_TARGET_MAX_S, int(kScaleS)};
  for (int t : ticks) {
    lv_obj_t *l = label(root, &font_caption, col::textOff, "");
    setTextFmt(l, t == int(kScaleS) ? "%d s" : "%d", t);
    lv_obj_set_pos(l, kBarX + kBarW * t / int32_t(kScaleS), kBarY + 22);
    lv_obj_set_style_translate_x(l, LV_PCT(-50), 0);  // centred on the tick
  }

  msg_ = label(root, &font_body_medium, col::textDim, "");
  lv_obj_set_width(msg_, LCD_WIDTH);
  lv_obj_set_style_text_align(msg_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(msg_, 0, 330);

  hist_ = box(root);  // not clickable: taps on the chips start / stop too
  lv_obj_set_size(hist_, LCD_WIDTH - 2 * kEdge, 40);
  lv_obj_align(hist_, LV_ALIGN_BOTTOM_MID, 0, -kBarBottom);
  lv_obj_set_flex_flow(hist_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hist_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hist_, 8, 0);

  lv_obj_set_clickable(root, true);                                // big tap target: hands are busy
  lv_obj_add_event_cb(root, onTap, LV_EVENT_SHORT_CLICKED, this);  // not after a long press
  lv_obj_add_event_cb(root, onLongPress, LV_EVENT_LONG_PRESSED, this);
  nextFrameMs_ = millis();
  render(millis());
  renderHistory();
}

void EspressoApp::onDestroy() { digits_ = fill_ = msg_ = hist_ = nullptr; }

void EspressoApp::onTick(uint32_t nowMs) {
  if (!digits_ || timer_.mode() != Mode::Running || !core::reached(nowMs, nextFrameMs_)) return;
  nextFrameMs_ = nowMs + kFrameMs;
  render(nowMs);
}

bool EspressoApp::onBack() {
  if (!timer_.back()) return false;
  render(millis());
  return true;
}

void EspressoApp::render(uint32_t nowMs) {
  const Mode mode = timer_.mode();
  const uint32_t ms = timer_.elapsedMs(nowMs);
  const uint32_t ds = mode == Mode::Done ? timer_.lastShotDs() : ms / 100;
  const Zone zone = shot::classify(mode == Mode::Done ? ds * 100 : ms, timer_.config());
  const uint32_t color = mode == Mode::Ready ? ui::col::text : zoneColor(zone);

  ui::setTextFmt(digits_, "%lu.%lu", (unsigned long)(ds / 10), (unsigned long)(ds % 10));
  lv_obj_set_style_text_color(digits_, ui::c(color), 0);
  const uint32_t w = mode == Mode::Ready ? 0 : min<uint32_t>(kBarW, uint64_t(ds) * kBarW / (kScaleS * 10));
  lv_obj_set_width(fill_, w);
  lv_obj_set_style_bg_color(fill_, ui::c(color), 0);

  if (mode == Mode::Ready) {
    ui::setText(msg_, "Tap anywhere to start");
    lv_obj_set_style_text_color(msg_, ui::c(ui::col::textDim), 0);
  } else if (mode == Mode::Running) {
    ui::setText(msg_, "Tap to stop");
    lv_obj_set_style_text_color(msg_, ui::c(ui::col::textDim), 0);
  } else {
    ui::setText(msg_, zone == Zone::InTarget ? "\u2713 In target" : zone == Zone::Over ? "Too long" : "Too short");
    lv_obj_set_style_text_color(msg_, ui::c(zoneColor(zone)), 0);
  }
}

// History chips, newest first (the running shot isn't in the history yet).
void EspressoApp::renderHistory() {
  lv_obj_clean(hist_);
  const uint8_t n = timer_.historyCount() < kChips ? timer_.historyCount() : kChips;
  for (uint8_t i = 0; i < n; i++) {
    const uint16_t v = timer_.historyAt(i);
    const bool ok = shot::classify(uint32_t(v) * 100, timer_.config()) == Zone::InTarget;
    lv_obj_t *chip = ui::toast(hist_, "");
    ui::setTextFmt(chip, ok ? "%u.%u ✓" : "%u.%u", v / 10, v % 10);
    lv_obj_set_style_text_color(chip, ui::c(ok ? ui::col::success : ui::col::textDim), 0);
    lv_obj_set_style_bg_color(chip, ui::c(ui::col::surface), 0);
    lv_obj_set_style_pad_hor(chip, 10, 0);  // four fit in a row
    lv_obj_set_style_pad_ver(chip, 6, 0);
  }
}

void EspressoApp::onTap(lv_event_t *e) {
  auto *self = static_cast<EspressoApp *>(lv_event_get_user_data(e));
  const bool wasRunning = self->timer_.mode() == Mode::Running;
  self->timer_.tap(millis());
  self->render(millis());
  if (wasRunning) {
    self->saveHistory();
    self->renderHistory();
  }
}

void EspressoApp::onLongPress(lv_event_t *e) {
  auto *self = static_cast<EspressoApp *>(lv_event_get_user_data(e));
  if (self->timer_.mode() != Mode::Done) return;
  self->timer_.discard();
  self->saveHistory();
  self->render(millis());
  self->renderHistory();
}

}  // namespace apps
