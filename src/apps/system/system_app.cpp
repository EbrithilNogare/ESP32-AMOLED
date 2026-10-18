#include "system_app.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board.h"
#include "config.h"
#include "core/board_init.h"
#include "core/net.h"
#include "core/ota.h"
#include "core/pmu.h"
#include "core/power.h"
#include "core/settings.h"
#include "core/ticks.h"
#include "core/touch.h"
#include "ui/power_anim.h"
#include "ui/theme.h"
#include "version.h"

namespace apps {

using core::ota::State;

static constexpr uint32_t kAccent = 0x8FA3BF;  // slate
static const AppInfo kInfo = {"system", "System", ICON_MEMORY, kAccent};
static const char *const kKeys[SystemApp::kRows] = {"Firmware", "Board", "Battery",  "Charging", "Standby",
                                                    "Wi-Fi",    "IP",    "Free RAM", "Uptime"};

const AppInfo &SystemApp::info() const { return kInfo; }

bool SystemApp::blocksNavigation() const {
  const State s = core::ota::state();
  return updateView_ && (s == State::Receiving || s == State::Done);
}

power::Policy SystemApp::powerPolicy() const { return updateView_ ? power::Policy::KeepAwake : power::Policy::Normal; }

void SystemApp::onCreate(lv_obj_t *root) {
  root_ = root;
  showInfo();
}

void SystemApp::onDestroy() {
  if (updateView_) core::ota::stop();
  if (touchView_) finishTouch(false);
  updateView_ = false;
  openUpdate_ = false;
  root_ = arc_ = icon_ = pct_ = status_ = line1_ = nullptr;
  for (auto &v : values_) v = nullptr;
}

bool SystemApp::onBack() {
  if (touchView_) {
    finishTouch(false);
    showInfo();
    return true;
  }
  if (!updateView_) return false;
  core::ota::stop();
  showInfo();
  return true;
}

static lv_obj_t *title(lv_obj_t *root, const char *text) {
  lv_obj_t *t = ui::label(root, &font_title, ui::col::text, text);
  lv_obj_set_pos(t, ui::kEdge, ui::kEdge - 2);  // cap height lines up with captions
  return t;
}

void SystemApp::showInfo() {
  using namespace ui;
  lv_obj_clean(root_);
  arc_ = icon_ = pct_ = status_ = line1_ = nullptr;
  updateView_ = false;
  title(root_, "System");

  lv_obj_t *list = box(root_);
  lv_obj_set_size(list, LCD_WIDTH - 40, LV_SIZE_CONTENT);
  lv_obj_set_pos(list, 20, 74);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  for (int i = 0; i < kRows; i++) {
    lv_obj_t *row = box(list);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, c(col::track), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_align(label(row, &font_list, col::textDim, kKeys[i]), LV_ALIGN_LEFT_MID, 0, 0);
    values_[i] = label(row, &font_list, col::text, "");
    lv_obj_align(values_[i], LV_ALIGN_RIGHT_MID, 0, 0);
  }

  const int32_t half = (LCD_WIDTH - 2 * kEdge - 12) / 2;
  lv_obj_t *btn = pill(root_, ICON_SYSTEM_UPDATE, "Update", col::text);
  lv_obj_set_width(btn, half);
  lv_obj_set_style_bg_color(btn, c(mixBlack(kAccent, 18)), 0);
  lv_obj_set_style_border_color(btn, c(kAccent), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, kEdge, -kBarBottom);
  lv_obj_add_event_cb(btn, onUpdateClicked, LV_EVENT_CLICKED, this);
  lv_obj_t *touch = pill(root_, ICON_STRAIGHTEN, "Touch", col::text);
  lv_obj_set_width(touch, half);
  lv_obj_set_style_bg_color(touch, c(col::surface), 0);
  lv_obj_align(touch, LV_ALIGN_BOTTOM_RIGHT, -kEdge, -kBarBottom);
  lv_obj_add_event_cb(touch, onTouchClicked, LV_EVENT_CLICKED, this);

  nextInfoMs_ = millis();
  updateInfo();
}

void SystemApp::updateInfo() {
  const int pct = core::pmu::batteryPercent();
  const uint32_t up = millis() / 1000;
  ui::setText(values_[0], APP_VERSION);
  ui::setTextFmt(values_[1], "%s · ESP32-S3R8", core::board::revisionName());
  if (pct >= 0)
    ui::setTextFmt(values_[2], "%d %% · %u.%02u V", pct, core::pmu::batteryMillivolts() / 1000,
                   (core::pmu::batteryMillivolts() % 1000) / 10);
  else
    ui::setText(values_[2], "no battery");
  ui::setText(values_[3], core::pmu::onCharger() ? (core::pmu::charging() ? "yes (USB)" : "full (USB)") : "no");
  // How long a charge lasts in deep sleep, measured across sleeps (ARCHITECTURE §8.6).
  const sleeplog::Log &sleep = core::power::sleepLog();
  const sleeplog::Estimate days = sleeplog::daysPerCharge(sleep);
  if (days.days)
    ui::setTextFmt(values_[4], "%s%lu days", days.atLeast ? "> " : "", (unsigned long)days.days);
  else if (sleep.totalS)
    ui::setTextFmt(values_[4], "measuring · %lu h", (unsigned long)(sleep.totalS / 3600));
  else
    ui::setText(values_[4], "–");
  if (core::net::connected())
    ui::setTextFmt(values_[5], "−%d dBm", -core::net::rssi());
  else
    ui::setText(values_[5], "off");
  ui::setText(values_[6], core::net::connected() ? core::net::ip() : "–");
  const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ui::setTextFmt(values_[7], "%u KB · %u.%u MB", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 unsigned(psram / 1048576), unsigned(psram % 1048576 * 10 / 1048576));
  ui::setTextFmt(values_[8], "%lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60),
                 (unsigned long)(up % 60));
}

void SystemApp::showUpdate() {
  using namespace ui;
  lv_obj_clean(root_);
  for (auto &v : values_) v = nullptr;
  updateView_ = true;
  doneAtMs_ = 0;
  restarting_ = false;
  title(root_, "Wireless update");

  arc_ = progressRing(root_, 260, 6);
  lv_obj_set_pos(arc_, 54, 70);
  lv_arc_set_range(arc_, 0, 100);
  lv_arc_set_value(arc_, 0);
  otaColor_ = 0;

  icon_ = label(root_, &font_icon_56, kAccent, ICON_WIFI_TETHERING);
  lv_obj_align(icon_, LV_ALIGN_TOP_MID, 0, 160);
  pct_ = label(root_, &font_digits_56, kAccent, "");
  lv_obj_align(pct_, LV_ALIGN_TOP_MID, 0, 164);
  status_ = label(root_, &font_caption, col::textDim, "");
  lv_obj_set_width(status_, 220);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, 232);

  line1_ = label(root_, &font_body_medium, col::text, "");
  lv_obj_set_width(line1_, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(line1_, LV_ALIGN_TOP_MID, 0, 352);
  lv_obj_t *line2 = label(root_, &font_caption, col::textDim, "");
  ui::setTextFmt(line2, "%s.local · %s", OTA_HOSTNAME, APP_VERSION);
  lv_obj_set_width(line2, LCD_WIDTH - 2 * kEdge);
  lv_obj_set_style_text_align(line2, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 384);

  core::ota::start();
  updateOta(millis());
}

void SystemApp::updateOta(uint32_t nowMs) {
  const State s = core::ota::state();
  uint32_t color = kAccent;
  const char *status = "";
  bool showPct = false;
  switch (s) {
    case State::WaitingForWifi: status = "Connecting to Wi-Fi…"; break;
    case State::Waiting: status = "Waiting for upload"; break;
    case State::Receiving:
      status = "Receiving";
      showPct = true;
      break;
    case State::Done:
      status = "Updated · restarting";
      color = ui::col::success;
      if (!doneAtMs_) doneAtMs_ = nowMs;
      break;
    case State::Error:
      status = core::ota::error();
      color = ui::col::error;
      break;
    case State::Off: break;
  }
  // This runs every loop pass while the screen is open: touch the styles only
  // when the colour changes (the arc value and texts skip unchanged values).
  if (color != otaColor_) {
    otaColor_ = color;
    lv_obj_set_style_arc_color(arc_, ui::c(color), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(icon_, ui::c(color), 0);
  }
  lv_arc_set_value(arc_, s == State::Done ? 100 : s == State::Receiving ? core::ota::progress() : 0);
  ui::setText(icon_, s == State::Done ? ICON_CHECK_CIRCLE : s == State::Error ? ICON_ERROR : ICON_WIFI_TETHERING);
  lv_obj_set_hidden(icon_, showPct);
  lv_obj_set_hidden(pct_, !showPct);
  if (showPct) ui::setTextFmt(pct_, "%u %%", core::ota::progress());
  ui::setText(status_, status);
  ui::setText(line1_, s == State::Receiving ? "Don’t power off" : core::net::connected() ? core::net::ip() : "");

  // After a successful update: show "Done" briefly, then restart with the
  // power-off animation; the new image confirms itself after its health check.
  if (s == State::Done && !restarting_ && nowMs - doneAtMs_ > 1400) {
    restarting_ = true;
    ui::power_anim::off([]() { ESP.restart(); });
  }
}

void SystemApp::onTick(uint32_t nowMs) {
  if (!root_) return;
  if (openUpdate_) {
    openUpdate_ = false;
    showUpdate();
  }
  const Pending p = pending_;
  pending_ = Pending::None;
  switch (p) {
    case Pending::Touch:
    case Pending::Redo: showTouch(); break;
    case Pending::Save:
      finishTouch(true);
      showInfo();
      break;
    case Pending::Factory:
      cal_ = touchcal::Cal{};  // no correction: the panel as it reports
      finishTouch(true);
      showInfo();
      break;
    case Pending::None: break;
  }
  if (touchView_) return;
  if (updateView_) {
    updateOta(nowMs);
  } else if (core::reached(nowMs, nextInfoMs_)) {
    nextInfoMs_ = nowMs + 1000;
    updateInfo();
  }
}

void SystemApp::onUpdateClicked(lv_event_t *e) {
  static_cast<SystemApp *>(lv_event_get_user_data(e))->openUpdate_ = true;
}

// ---------------------------------------------------------------- touch calibration

// Near the corners, where a panel's error shows most, and the centre.
static const int16_t kCalX[] = {40, LCD_WIDTH - 40, LCD_WIDTH - 40, 40, LCD_WIDTH / 2};
static const int16_t kCalY[] = {40, 40, LCD_HEIGHT - 40, LCD_HEIGHT - 40, LCD_HEIGHT / 2};
static constexpr int32_t kCross = 56, kMaxMissPx = 24;

void SystemApp::onTouchClicked(lv_event_t *e) {
  static_cast<SystemApp *>(lv_event_get_user_data(e))->pending_ = Pending::Touch;
}

// The crosses are tapped with the correction off (raw panel positions); the
// fit is then applied right away, and kept only when saved.
void SystemApp::showTouch() {
  using namespace ui;
  lv_obj_clean(root_);
  for (auto &v : values_) v = nullptr;
  touchView_ = true;
  calStep_ = 0;
  core::touch::setRaw(true);

  hint_ = label(root_, &font_body, col::text, "Tap the centre of the cross");
  lv_obj_set_width(hint_, LCD_WIDTH - 2 * 40);
  lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hint_, LV_ALIGN_CENTER, 0, -76);
  step_ = label(root_, &font_caption, col::textDim, "");
  lv_obj_align(step_, LV_ALIGN_CENTER, 0, 60);

  cross_ = box(root_);
  lv_obj_set_size(cross_, kCross, kCross);
  lv_obj_t *ring = box(cross_);
  lv_obj_set_size(ring, 28, 28);
  lv_obj_center(ring);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, c(kAccent), 0);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *bar = box(cross_);
    lv_obj_set_size(bar, i ? 2 : kCross, i ? kCross : 2);
    lv_obj_center(bar);
    lv_obj_set_style_bg_color(bar, c(col::text), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }

  dot_ = box(root_);
  lv_obj_set_size(dot_, 24, 24);
  lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot_, c(kAccent), 0);
  lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
  lv_obj_set_hidden(dot_, true);

  // The whole screen takes the taps (a tap off the cross still counts), under
  // the Redo / Save buttons of the check that follows.
  lv_obj_t *catcher = box(root_);
  lv_obj_set_size(catcher, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_clickable(catcher, true);
  lv_obj_add_event_cb(catcher, onCatcher, LV_EVENT_ALL, this);

  buttons_ = box(root_);
  lv_obj_set_size(buttons_, LCD_WIDTH, kBtn);
  lv_obj_align(buttons_, LV_ALIGN_BOTTOM_MID, 0, -kBarBottom);
  const int32_t half = (LCD_WIDTH - 2 * kEdge - 12) / 2;
  lv_obj_t *redo = textButton(buttons_, "Redo", col::text);
  lv_obj_set_width(redo, half);
  lv_obj_align(redo, LV_ALIGN_LEFT_MID, kEdge, 0);
  lv_obj_add_event_cb(redo, onTouchButton, LV_EVENT_CLICKED, this);
  lv_obj_t *save = textButton(buttons_, "Save", mixBlack(kAccent, 15));
  lv_obj_set_style_bg_color(save, c(kAccent), 0);
  lv_obj_set_width(save, half);
  lv_obj_align(save, LV_ALIGN_RIGHT_MID, -kEdge, 0);
  lv_obj_set_user_data(save, reinterpret_cast<void *>(1));
  lv_obj_add_event_cb(save, onTouchButton, LV_EVENT_CLICKED, this);
  lv_obj_set_hidden(buttons_, true);

  // While tapping crosses: back to the factory calibration (no correction),
  // e.g. when a bad one makes the crosses hard to hit. A big target between
  // the centre cross and the bottom ones.
  factory_ = textButton(root_, "Reset to factory", col::textDim, kBtnSmall);
  lv_obj_set_width(factory_, 228);
  lv_obj_align(factory_, LV_ALIGN_TOP_MID, 0, 302);
  lv_obj_set_style_bg_color(factory_, c(col::surface), 0);
  lv_obj_add_event_cb(factory_, onFactory, LV_EVENT_CLICKED, this);
  placeCross();
}

void SystemApp::onFactory(lv_event_t *e) {
  static_cast<SystemApp *>(lv_event_get_user_data(e))->pending_ = Pending::Factory;
}

void SystemApp::placeCross() {
  lv_obj_set_pos(cross_, kCalX[calStep_] - kCross / 2, kCalY[calStep_] - kCross / 2);
  ui::setTextFmt(step_, "%d / %d", calStep_ + 1, kCalTargets);
}

void SystemApp::onCatcher(lv_event_t *e) {
  auto *self = static_cast<SystemApp *>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p{0, 0};
  lv_indev_get_point(lv_indev_active(), &p);
  if (self->calStep_ == kCalTargets) {  // checking: the dot follows the finger
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
      lv_obj_set_pos(self->dot_, p.x - 12, p.y - 12);
      lv_obj_set_hidden(self->dot_, false);
    }
    return;
  }
  if (code != LV_EVENT_RELEASED) return;
  const int i = self->calStep_;
  self->samples_[i] = {int16_t(p.x), int16_t(p.y), kCalX[i], kCalY[i]};
  if (++self->calStep_ < kCalTargets) {
    self->placeCross();
    return;
  }
  if (!touchcal::fit(self->samples_, kCalTargets, kMaxMissPx, self->cal_)) {
    self->calStep_ = 0;
    self->placeCross();
    ui::setText(self->hint_, "That didn't line up. Tap each cross again");
    return;
  }
  core::touch::setCalibration(self->cal_);
  core::touch::setRaw(false);
  lv_obj_set_hidden(self->cross_, true);
  lv_obj_set_hidden(self->step_, true);
  lv_obj_set_hidden(self->factory_, true);
  lv_obj_set_hidden(self->buttons_, false);
  ui::setText(self->hint_, "Drag around: the dot should follow your finger");
}

void SystemApp::onTouchButton(lv_event_t *e) {
  auto *self = static_cast<SystemApp *>(lv_event_get_user_data(e));
  const bool save = lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_current_target(e))) != nullptr;
  self->pending_ = save ? Pending::Save : Pending::Redo;
}

// Leaves calibration: keeps the new correction, or goes back to the saved one.
void SystemApp::finishTouch(bool save) {
  touchView_ = false;
  core::touch::setRaw(false);
  if (save) core::settings::saveTouchCal(cal_);
  core::touch::setCalibration(core::settings::touchCal());
  cross_ = dot_ = hint_ = step_ = buttons_ = factory_ = nullptr;
}

}  // namespace apps
