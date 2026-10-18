#include "pomodoro_app.h"

#include <Arduino.h>
#include <math.h>

#include <type_traits>

#include "apps/app_manager.h"
#include "board.h"
#include "config.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

using pomo::Phase;

static const AppInfo kInfo = {"pomodoro", "Pomodoro", ICON_TIMER, 0xFF5A4E};  // tomato
static constexpr int32_t kMinRadius = 62;
static constexpr uint32_t kConfirmResetMs = 2000;
static_assert(POMO_SET_SIZE >= 1 && POMO_SET_SIZE <= 8, "the set dots fit up to 8 sessions");
static_assert(std::is_trivially_copyable<pomo::Pomodoro>::value, "parked in RTC memory as is");

// PomodoroCore keeps absolute timestamps, on the clock that doesn't wrap and
// counts on through deep sleep.
static uint64_t now64(uint32_t ms) { return core::mono(ms); }
static uint64_t now64() { return core::mono(millis()); }

static uint32_t phaseColor(Phase p) {
  switch (p) {
    case Phase::ShortBreak: return ui::col::success;
    case Phase::LongBreak: return ui::col::info;
    default: return kInfo.accent;  // focus / idle
  }
}

static const char *phaseName(Phase p) {
  switch (p) {
    case Phase::ShortBreak: return "SHORT BREAK";
    case Phase::LongBreak: return "LONG BREAK";
    default: return "FOCUS";
  }
}

PomodoroApp::PomodoroApp()
    : timer_({POMO_FOCUS_MIN * 60000UL, POMO_SHORT_BREAK_MIN * 60000UL, POMO_LONG_BREAK_MIN * 60000UL, POMO_SET_SIZE,
              POMO_AUTO_START_NEXT != 0}) {}

const AppInfo &PomodoroApp::info() const { return kInfo; }

power::Policy PomodoroApp::powerPolicy() const {
  return timer_.running() ? power::Policy::LowPowerDisplay : power::Policy::Normal;
}
power::Policy PomodoroApp::backgroundPolicy() const { return powerPolicy(); }

// Sleep: the raw timer goes to RTC memory, its end time still good on the
// clock that counted through the sleep. Cold boot or power-off: the paused
// state from NVS (written only at a power-off: flash wear).
void PomodoroApp::onWake(Wake wake) {
  if (wake != Wake::Cold && core::store::unpark("pomo", &timer_, sizeof timer_)) return;
  pomo::Saved s;
  if (core::store::load("pomo", &s, sizeof s)) timer_.restore(s, now64());
}

void PomodoroApp::service(uint32_t nowMs) {
  if (timer_.update(now64(nowMs))) {
    // Phase end: beep and bring the full view to the front with the screen on
    // (ends a minimal view; after a deep sleep this is what woke it).
    if (POMO_SOUND) core::sound::play(core::sound::Pattern::PhaseEnd);
    apps::manager::attention(this);
    flash();
    lastRenderSec_ = UINT32_MAX;
  }
}

sleepplan::Need PomodoroApp::sleepNeed(uint64_t monoNow) const {
  return {false, timer_.running() ? monoNow + timer_.remainingMs(monoNow) : 0};
}

void PomodoroApp::onSleep(sleepplan::Kind kind) {
  const uint64_t now = now64();
  if (kind == sleepplan::Kind::PowerOff) {
    timer_.pause(now);  // power-off pauses the timer
    pomo::Saved s = timer_.save(now);
    core::store::save("pomo", &s, sizeof s);
  }
  core::store::park("pomo", &timer_, sizeof timer_);
}

// ---------------------------------------------------------------- full view

void PomodoroApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  root_ = root;

  phase_ = header(root, "", phaseColor(timer_.phase()));
  for (int i = 0; i < POMO_SET_SIZE; i++) {
    dots_[i] = box(root);
    lv_obj_set_size(dots_[i], 10, 10);
    lv_obj_set_pos(dots_[i], LCD_WIDTH - kEdge - 10 - (POMO_SET_SIZE - 1 - i) * 18, kEdge + 4);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots_[i], 2, 0);
  }

  arc_ = progressRing(root, 300, 6);  // not clickable: the centre area handles taps
  lv_obj_set_pos(arc_, 34, 66);

  lv_obj_t *centre = box(root);
  lv_obj_set_size(centre, 260, 260);
  lv_obj_set_pos(centre, 54, 86);
  lv_obj_set_clickable(centre, true);
  lv_obj_add_event_cb(centre, onRing, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_event_cb(centre, onRing, LV_EVENT_LONG_PRESSED, this);
  digits_ = label(centre, &font_digits_80, col::text, "");
  lv_obj_align(digits_, LV_ALIGN_CENTER, 0, -8);
  sub_ = label(centre, &font_caption, col::textDim, "");
  lv_obj_align(sub_, LV_ALIGN_CENTER, 0, 44);

  lv_obj_t *row = box(root);
  lv_obj_set_size(row, LCD_WIDTH, kBtn);
  lv_obj_set_pos(row, 0, LCD_HEIGHT - kBarBottom - kBtn);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 16, 0);
  toggle_ = pill(row, ICON_PLAY_ARROW, "Start", phaseColor(timer_.phase()));
  lv_obj_add_event_cb(toggle_, onToggle, LV_EVENT_CLICKED, this);
  lv_obj_t *reset = roundButton(row, ICON_RESTART_ALT, col::textDim);
  lv_obj_add_event_cb(reset, onReset, LV_EVENT_CLICKED, this);

  toast_ = toast(root, "Tap again to reset");
  lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -(kBarBottom + kBtn + 14));
  lv_obj_set_hidden(toast_, true);
  confirmResetUntil_ = 0;

  lastRenderSec_ = UINT32_MAX;
  render(millis());
}

void PomodoroApp::onDestroy() {
  root_ = phase_ = arc_ = digits_ = sub_ = toggle_ = toast_ = nullptr;
  for (auto &d : dots_) d = nullptr;
}

void PomodoroApp::onTick(uint32_t nowMs) {
  if (!root_) return;
  if (confirmResetUntil_ && core::reached(nowMs, confirmResetUntil_)) {
    confirmResetUntil_ = 0;
    lv_obj_set_hidden(toast_, true);
  }
  render(nowMs);
}

void PomodoroApp::render(uint32_t nowMs) {
  const uint32_t rem = timer_.remainingMs(now64(nowMs));
  const uint32_t sec = (rem + 999) / 1000;
  if (sec == lastRenderSec_) return;  // the display only changes once a second
  lastRenderSec_ = sec;

  const Phase ph = timer_.phase();
  const uint32_t color = phaseColor(ph);
  ui::setText(phase_, phaseName(ph));
  lv_obj_set_style_text_color(phase_, ui::c(color), 0);
  const int filled = timer_.completed();  // focus sessions done in this set
  for (int i = 0; i < POMO_SET_SIZE; i++) {
    lv_obj_set_style_border_color(dots_[i], ui::c(color), 0);
    lv_obj_set_style_bg_color(dots_[i], ui::c(color), 0);
    lv_obj_set_style_bg_opa(dots_[i], i < filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }

  const uint32_t dur = timer_.phaseDurationMs();
  lv_obj_set_style_arc_color(arc_, ui::c(color), LV_PART_INDICATOR);
  lv_arc_set_value(arc_, dur ? int32_t(1000 - uint64_t(rem) * 1000 / dur) : 0);
  ui::setTextFmt(digits_, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));

  if (timer_.running()) {
    const Phase next = timer_.nextPhase();
    ui::setTextFmt(sub_, "until %s",
                   next == Phase::Focus       ? "focus"
                   : next == Phase::LongBreak ? "long break"
                   : next == Phase::Idle      ? "the end of the set"
                                              : "short break");
    ui::setPillContent(toggle_, ICON_PAUSE, "Pause");
  } else {
    const bool fresh = ph == Phase::Idle || rem == dur;
    ui::setText(sub_, ph == Phase::Idle ? "ready" : "paused");
    ui::setPillContent(toggle_, ICON_PLAY_ARROW, fresh ? "Start" : "Resume");
  }
  lv_obj_set_style_text_color(toggle_, ui::c(color), 0);
}

void PomodoroApp::flash() {
  if (root_) ui::flash(root_, phaseColor(timer_.phase()));
}

void PomodoroApp::onToggle(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  self->timer_.toggle(now64());
  self->lastRenderSec_ = UINT32_MAX;
}

void PomodoroApp::onReset(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  const uint32_t now = millis();
  if (self->confirmResetUntil_ && !core::reached(now, self->confirmResetUntil_)) {
    self->timer_.reset(now64(now));
    self->confirmResetUntil_ = 0;
    lv_obj_set_hidden(self->toast_, true);
  } else {
    self->confirmResetUntil_ = now + kConfirmResetMs;
    lv_obj_set_hidden(self->toast_, false);
  }
  self->lastRenderSec_ = UINT32_MAX;
}

void PomodoroApp::onRing(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED)
    self->timer_.skip(now64());
  else
    self->timer_.toggle(now64());
  self->lastRenderSec_ = UINT32_MAX;
}

// ------------------------------------------------------------- minimal view

void PomodoroApp::enterMinimal(lv_obj_t *layer) {
  minLayer_ = layer;
  for (auto &d : minDots_) {
    d = ui::box(layer);
    lv_obj_set_size(d, 5, 5);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  }
  minDigits_ = ui::label(layer, &font_digits_44, phaseColor(timer_.phase()), "");
  lv_obj_set_width(minDigits_, 140);
  lv_obj_set_style_text_align(minDigits_, LV_TEXT_ALIGN_CENTER, 0);
  renderMinimal(millis());
}

void PomodoroApp::exitMinimal() {
  minLayer_ = minDigits_ = nullptr;
  for (auto &d : minDots_) d = nullptr;
  lastRenderSec_ = UINT32_MAX;  // the full view may be stale after light sleep
}

void PomodoroApp::minimalTick(uint32_t nowMs) {
  service(nowMs);
  if (minLayer_) renderMinimal(nowMs);
}

uint32_t PomodoroApp::minimalRefreshMs(uint32_t nowMs) const {
  // Wake at the next minute boundary of the countdown (or the phase end).
  const uint32_t rem = timer_.remainingMs(now64(nowMs));
  const uint32_t toBoundary = rem % 60000;
  return toBoundary ? toBoundary + 50 : 60000;
}

// Sparse view: minutes plus a dotted ring of the remaining time, drifting a
// few pixels every refresh against burn-in (UI_DESIGN §6).
void PomodoroApp::renderMinimal(uint32_t nowMs) {
  const uint32_t rem = timer_.remainingMs(now64(nowMs));
  const uint32_t dur = timer_.phaseDurationMs();
  const uint32_t minutes = (rem + 59999) / 60000;
  const uint32_t color = phaseColor(timer_.phase());
  const lv_point_t d = ui::drift(int32_t(minutes), 22, 26);
  const int32_t cx = LCD_WIDTH / 2 + d.x, cy = LCD_HEIGHT / 2 + d.y;
  const int lit = dur ? int((uint64_t(rem) * kMinDots + dur - 1) / dur) : 0;
  for (int i = 0; i < kMinDots; i++) {
    const float a = -float(M_PI) / 2 + i * 2 * float(M_PI) / kMinDots;
    lv_obj_set_pos(minDots_[i], cx + lroundf(kMinRadius * cosf(a)) - 2, cy + lroundf(kMinRadius * sinf(a)) - 2);
    lv_obj_set_style_bg_color(minDots_[i], ui::c(color), 0);
    lv_obj_set_hidden(minDots_[i], i >= lit);
  }
  ui::setTextFmt(minDigits_, "%lu", (unsigned long)minutes);
  lv_obj_set_style_text_color(minDigits_, ui::c(color), 0);
  lv_obj_set_pos(minDigits_, cx - 70, cy - 24);
}

}  // namespace apps
