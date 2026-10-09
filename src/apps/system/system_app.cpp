#include "system_app.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board.h"
#include "config.h"
#include "core/board_init.h"
#include "core/net.h"
#include "core/ota.h"
#include "core/pmu.h"
#include "ui/power_anim.h"
#include "ui/theme.h"

namespace apps {

using core::ota::State;

static const AppInfo kInfo = {"system", "System", ICON_MEMORY, 0x8FA3BF};
static constexpr uint32_t kAccent = 0x8FA3BF;
static const char *const kKeys[8] = {"Firmware", "Board", "Battery", "Charging", "Wi-Fi", "IP", "Free RAM", "Uptime"};

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
  updateView_ = false;
  root_ = arc_ = icon_ = pct_ = status_ = line1_ = nullptr;
  for (auto &v : values_) v = nullptr;
}

bool SystemApp::onBack() {
  if (!updateView_) return false;
  core::ota::stop();
  showInfo();
  return true;
}

static lv_obj_t *title(lv_obj_t *root, const char *text) {
  lv_obj_t *t = ui::label(root, &font_title, ui::col::text, text);
  lv_obj_set_pos(t, 24, 22);
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
  for (int i = 0; i < 8; i++) {
    lv_obj_t *row = box(list);
    lv_obj_set_size(row, LV_PCT(100), 35);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, c(0x1C1C1C), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_align(label(row, &font_list, col::textDim, kKeys[i]), LV_ALIGN_LEFT_MID, 0, 0);
    values_[i] = label(row, &font_list, col::text, "");
    lv_obj_align(values_[i], LV_ALIGN_RIGHT_MID, 0, 0);
  }

  lv_obj_t *btn = pill(root_, ICON_SYSTEM_UPDATE, "Wireless update", 0xC9D6E8);
  lv_obj_set_width(btn, LCD_WIDTH - 48);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(btn, c(0x18202A), 0);
  lv_obj_set_style_border_color(btn, c(kAccent), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_add_event_cb(btn, onUpdateClicked, LV_EVENT_CLICKED, this);

  nextInfoMs_ = 0;
  updateInfo();
}

void SystemApp::updateInfo() {
  const int pct = core::pmu::batteryPercent();
  const uint32_t up = millis() / 1000;
  lv_label_set_text(values_[0], APP_VERSION);
  lv_label_set_text_fmt(values_[1], "%s · ESP32-S3R8", core::board::revisionName());
  if (pct >= 0)
    lv_label_set_text_fmt(values_[2], "%d %% · %u.%02u V", pct, core::pmu::batteryMillivolts() / 1000,
                          (core::pmu::batteryMillivolts() % 1000) / 10);
  else
    lv_label_set_text(values_[2], "no battery");
  lv_label_set_text(values_[3], core::pmu::onCharger() ? (core::pmu::charging() ? "yes (USB)" : "full (USB)") : "no");
  if (core::net::connected())
    lv_label_set_text_fmt(values_[4], "−%d dBm", -core::net::rssi());
  else
    lv_label_set_text(values_[4], "off");
  lv_label_set_text(values_[5], core::net::connected() ? core::net::ip() : "–");
  lv_label_set_text_fmt(values_[6], "%u KB · %u.%u MB", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576),
                        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) % 1048576 * 10 / 1048576));
  lv_label_set_text_fmt(values_[7], "%lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60),
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

  arc_ = lv_arc_create(root_);
  lv_obj_set_size(arc_, 260, 260);
  lv_obj_set_pos(arc_, 54, 70);
  lv_arc_set_rotation(arc_, 270);
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_range(arc_, 0, 100);
  lv_arc_set_value(arc_, 0);
  lv_obj_set_style_arc_width(arc_, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_, c(col::track), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);

  icon_ = label(root_, &font_icon_56, kAccent, ICON_WIFI_TETHERING);
  lv_obj_align(icon_, LV_ALIGN_TOP_MID, 0, 160);
  pct_ = label(root_, &font_digits_56, kAccent, "");
  lv_obj_align(pct_, LV_ALIGN_TOP_MID, 0, 164);
  status_ = label(root_, &font_caption, col::textDim, "");
  lv_obj_set_width(status_, 220);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, 232);

  line1_ = label(root_, &font_body_medium, col::text, "");
  lv_obj_set_width(line1_, LCD_WIDTH - 48);
  lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(line1_, LV_ALIGN_TOP_MID, 0, 352);
  lv_obj_t *line2 = label(root_, &font_caption, col::textDim, "");
  lv_label_set_text_fmt(line2, "%s.local · %s", OTA_HOSTNAME, APP_VERSION);
  lv_obj_set_width(line2, LCD_WIDTH - 48);
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
  lv_obj_set_style_arc_color(arc_, ui::c(color), LV_PART_INDICATOR);
  lv_arc_set_value(arc_, s == State::Done ? 100 : s == State::Receiving ? core::ota::progress() : 0);
  lv_label_set_text(icon_, s == State::Done ? ICON_CHECK_CIRCLE : s == State::Error ? ICON_ERROR : ICON_WIFI_TETHERING);
  lv_obj_set_style_text_color(icon_, ui::c(color), 0);
  if (showPct) {
    lv_obj_set_hidden(icon_, true);
    lv_obj_set_hidden(pct_, false);
    lv_label_set_text_fmt(pct_, "%u %%", core::ota::progress());
  } else {
    lv_obj_set_hidden(icon_, false);
    lv_obj_set_hidden(pct_, true);
  }
  lv_label_set_text(status_, status);
  lv_label_set_text(line1_, s == State::Receiving ? "Don’t power off" : core::net::connected() ? core::net::ip() : "");

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
  if (updateView_) {
    updateOta(nowMs);
  } else if (nowMs >= nextInfoMs_) {
    nextInfoMs_ = nowMs + 1000;
    updateInfo();
  }
}

void SystemApp::onUpdateClicked(lv_event_t *e) {
  static_cast<SystemApp *>(lv_event_get_user_data(e))->openUpdate_ = true;
}

}  // namespace apps
