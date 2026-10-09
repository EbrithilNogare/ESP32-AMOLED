#include "pomodoro_app.h"

#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "config.h"
#include "core/power.h"
#include "core/sound.h"
#include "core/store.h"
#include "ui/theme.h"

namespace apps {

using pomo::Phase;

static const AppInfo kInfo = {"pomodoro", "Pomodoro", ICON_TIMER, 0xFF5A4E};
static constexpr int kMinDots = 40;
static constexpr int32_t kMinRadius = 62;

static uint32_t phaseColor(Phase p) {
  switch (p) {
    case Phase::ShortBreak: return ui::col::success;
    case Phase::LongBreak: return ui::col::info;
    default: return 0xFF5A4E;  // focus / idle: tomato
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

void PomodoroApp::service(uint32_t nowMs) {
  if (!loaded_) {  // NVS is ready by the first loop, not at static construction
    pomo::Saved s;
    if (core::store::load("pomo", &s, sizeof s)) timer_.restore(s, nowMs);
    loaded_ = true;
  }
  if (timer_.update(nowMs)) {
    // Phase end: beep and show the full view for a while (it wakes the
    // screen like a touch would, which also ends the minimal view).
    if (POMO_SOUND) core::sound::play(core::sound::Pattern::PhaseEnd);
    core::power::activity();
    flash();
    lastRenderSec_ = UINT32_MAX;
  }
}

void PomodoroApp::onSleep() {
  const uint32_t now = millis();
  if (timer_.running()) timer_.pause(now);  // a long-press sleep pauses the timer
  pomo::Saved s = timer_.save(now);
  core::store::save("pomo", &s, sizeof s);
}

// ---------------------------------------------------------------- full view

void PomodoroApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  root_ = root;

  phase_ = label(root, &font_caption, phaseColor(timer_.phase()), "");
  lv_obj_set_style_text_letter_space(phase_, 1, 0);
  lv_obj_set_pos(phase_, 24, 24);
  for (int i = 0; i < 4; i++) {
    dots_[i] = box(root);
    lv_obj_set_size(dots_[i], 10, 10);
    lv_obj_set_pos(dots_[i], LCD_WIDTH - 24 - 10 - (3 - i) * 18, 28);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots_[i], 2, 0);
  }

  arc_ = lv_arc_create(root);
  lv_obj_set_size(arc_, 300, 300);
  lv_obj_set_pos(arc_, 34, 66);
  lv_arc_set_rotation(arc_, 270);
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_range(arc_, 0, 1000);
  lv_obj_set_style_arc_width(arc_, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_, c(col::track), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc_, 0, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);  // the centre area handles taps

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
  lv_obj_set_size(row, LCD_WIDTH, 56);
  lv_obj_set_pos(row, 0, LCD_HEIGHT - 22 - 56);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 16, 0);
  toggle_ = pill(row, ICON_PLAY_ARROW, "Start", phaseColor(timer_.phase()));
  lv_obj_add_event_cb(toggle_, onToggle, LV_EVENT_CLICKED, this);
  lv_obj_t *reset = roundButton(row, ICON_RESTART_ALT, col::textDim);
  lv_obj_add_event_cb(reset, onReset, LV_EVENT_CLICKED, this);

  toast_ = label(root, &font_caption, col::text, "Tap again to reset");
  lv_obj_set_style_bg_color(toast_, c(col::surfaceHi), 0);
  lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(toast_, 12, 0);
  lv_obj_set_style_pad_hor(toast_, 14, 0);
  lv_obj_set_style_pad_ver(toast_, 8, 0);
  lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -92);
  lv_obj_set_hidden(toast_, true);

  lastRenderSec_ = UINT32_MAX;
  render(millis());
}

void PomodoroApp::onDestroy() {
  root_ = phase_ = arc_ = digits_ = sub_ = toggle_ = toast_ = nullptr;
  for (auto &d : dots_) d = nullptr;
}

void PomodoroApp::onTick(uint32_t nowMs) {
  if (!root_) return;
  if (confirmResetUntil_ && nowMs > confirmResetUntil_) {
    confirmResetUntil_ = 0;
    lv_obj_set_hidden(toast_, true);
  }
  render(nowMs);
}

void PomodoroApp::render(uint32_t nowMs) {
  const uint32_t rem = timer_.remainingMs(nowMs);
  const uint32_t sec = (rem + 999) / 1000;
  if (sec == lastRenderSec_) return;  // the display only changes once a second
  lastRenderSec_ = sec;

  const Phase ph = timer_.phase();
  const uint32_t color = phaseColor(ph);
  lv_label_set_text(phase_, phaseName(ph));
  lv_obj_set_style_text_color(phase_, ui::c(color), 0);
  const int filled = timer_.completed() < 4 ? timer_.completed() : 4;  // focus sessions done in this set
  for (int i = 0; i < 4; i++) {
    lv_obj_set_style_border_color(dots_[i], ui::c(color), 0);
    lv_obj_set_style_bg_color(dots_[i], ui::c(color), 0);
    lv_obj_set_style_bg_opa(dots_[i], i < filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }

  const uint32_t dur = timer_.phaseDurationMs();
  lv_obj_set_style_arc_color(arc_, ui::c(color), LV_PART_INDICATOR);
  lv_arc_set_value(arc_, dur ? int32_t(1000 - uint64_t(rem) * 1000 / dur) : 0);
  lv_label_set_text_fmt(digits_, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));

  if (timer_.running()) {
    const Phase next = timer_.nextPhase();
    lv_label_set_text_fmt(sub_, "until %s",
                          next == Phase::Focus       ? "focus"
                          : next == Phase::LongBreak ? "long break"
                          : next == Phase::Idle      ? "the end of the set"
                                                     : "short break");
    ui::setPillContent(toggle_, ICON_PAUSE, "Pause");
  } else {
    const bool fresh = ph == Phase::Idle || rem == dur;
    lv_label_set_text(sub_, ph == Phase::Idle ? "ready" : "paused");
    ui::setPillContent(toggle_, ICON_PLAY_ARROW, fresh ? "Start" : "Resume");
  }
  lv_obj_set_style_text_color(toggle_, ui::c(color), 0);
}

void PomodoroApp::flash() {
  if (!root_) return;
  lv_obj_t *f = ui::box(root_);
  lv_obj_set_size(f, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(f, ui::c(phaseColor(timer_.phase())), 0);
  lv_obj_set_clickable(f, false);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, f);
  lv_anim_set_values(&a, 0, 90);  // ~35 % at the peak
  lv_anim_set_duration(&a, 400);
  lv_anim_set_playback_duration(&a, 400);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, lv_opa_t(v), 0); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_obj_delete((lv_obj_t *)an->var); });
  lv_anim_start(&a);
}

void PomodoroApp::onToggle(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  self->timer_.toggle(millis());
  self->lastRenderSec_ = UINT32_MAX;
}

void PomodoroApp::onReset(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  const uint32_t now = millis();
  if (self->confirmResetUntil_ && now <= self->confirmResetUntil_) {
    self->timer_.reset(now);
    self->confirmResetUntil_ = 0;
    lv_obj_set_hidden(self->toast_, true);
  } else {
    self->confirmResetUntil_ = now + 2000;
    lv_obj_set_hidden(self->toast_, false);
  }
  self->lastRenderSec_ = UINT32_MAX;
}

void PomodoroApp::onRing(lv_event_t *e) {
  auto *self = static_cast<PomodoroApp *>(lv_event_get_user_data(e));
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED)
    self->timer_.skip(millis());
  else
    self->timer_.toggle(millis());
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
  const uint32_t rem = timer_.remainingMs(nowMs);
  const uint32_t toBoundary = rem % 60000;
  return toBoundary ? toBoundary + 50 : 60000;
}

// Sparse view: minutes plus a dotted ring of the remaining time, drifting a
// few pixels every refresh against burn-in (UI_DESIGN §6).
void PomodoroApp::renderMinimal(uint32_t nowMs) {
  const uint32_t rem = timer_.remainingMs(nowMs);
  const uint32_t dur = timer_.phaseDurationMs();
  const uint32_t minutes = (rem + 59999) / 60000;
  const uint32_t color = phaseColor(timer_.phase());
  const int32_t k = int32_t(minutes);
  const int32_t cx = LCD_WIDTH / 2 + int32_t(lroundf(sinf(k * 1.7f) * 22));
  const int32_t cy = LCD_HEIGHT / 2 + int32_t(lroundf(cosf(k * 1.1f) * 26));
  const int lit = dur ? int((uint64_t(rem) * kMinDots + dur - 1) / dur) : 0;
  for (int i = 0; i < kMinDots; i++) {
    const float a = -float(M_PI) / 2 + i * 2 * float(M_PI) / kMinDots;
    lv_obj_set_pos(minDots_[i], cx + lroundf(kMinRadius * cosf(a)) - 2, cy + lroundf(kMinRadius * sinf(a)) - 2);
    lv_obj_set_style_bg_color(minDots_[i], ui::c(color), 0);
    if (i < lit)
      lv_obj_set_hidden(minDots_[i], false);
    else
      lv_obj_set_hidden(minDots_[i], true);
  }
  lv_label_set_text_fmt(minDigits_, "%lu", (unsigned long)minutes);
  lv_obj_set_style_text_color(minDigits_, ui::c(color), 0);
  lv_obj_set_pos(minDigits_, cx - 70, cy - 24);
}

}  // namespace apps
