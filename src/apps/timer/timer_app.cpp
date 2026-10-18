#include "timer_app.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <type_traits>

#include "apps/app_manager.h"
#include "board.h"
#include "config.h"
#include "core/power.h"
#include "core/rtc_clock.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps {

using timer::State;

static const AppInfo kInfo = {"timer", "Timer", ICON_ALARM, TIMER_COLOR};
static constexpr int32_t kTabY = 20;
static constexpr int32_t kHhY = 172, kMmY = 304;    // clock: centres of the two lines
static constexpr int32_t kColW = 120, kColH = 236;  // countdown: one wheel column
static constexpr int32_t kColY = 92, kColGap = 40;  // ... its top, and the gap for the colon
static constexpr int32_t kWheelStepPx = 16;         // drag distance per step
static constexpr int32_t kDragSlop = 10;
static constexpr int32_t kRing = 260, kRingY = 88;
static_assert(TIMER_DEFAULT_COUNTDOWN_S > 0 && TIMER_DEFAULT_COUNTDOWN_S <= timer::kMaxSeconds, "1 s .. 99:59");
static_assert(std::is_trivially_copyable<timer::Stopwatch>::value &&
                  std::is_trivially_copyable<timer::Countdown>::value,
              "parked in RTC memory as is");

static const char *const kTabs[3] = {"Clock", "Stopwatch", "Countdown"};
static const char *const kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Both timers keep absolute timestamps, on the clock that doesn't wrap and
// counts on through deep sleep.
static uint64_t now64(uint32_t ms) { return core::mono(ms); }

static bool localNow(struct tm &t) {
  if (!core::clock::valid()) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

// mm:ss, or h:mm:ss from an hour up. Overtime ("-") drops the leading zero
// so it stays five characters wide.
static void fmtTime(char *buf, size_t n, uint32_t s, const char *sign = "") {
  if (s >= 3600)
    snprintf(buf, n, "%s%lu:%02lu:%02lu", sign, (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60));
  else
    snprintf(buf, n, sign[0] ? "%s%lu:%02lu" : "%s%02lu:%02lu", sign, (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Up to five characters fit in the ring at 80 px; longer ones get 56 px.
static const lv_font_t *ringFont(const char *text) { return strlen(text) > 5 ? &font_digits_56 : &font_digits_80; }

const AppInfo &TimerApp::info() const { return kInfo; }

// ---------------------------------------------------------------- logic

void TimerApp::onWake(Wake wake) {
  Saved s;
  if (core::store::load("timer", &s, sizeof s)) {
    cd_.set(s.countdownS);
    tab_ = s.tab < 3 ? Tab(s.tab) : Tab::Clock;
  } else {
    cd_.set(TIMER_DEFAULT_COUNTDOWN_S);
  }
  Parked p;
  if (wake != Wake::Cold && core::store::unpark("timer", &p, sizeof p)) {
    sw_ = p.sw;
    cd_ = p.cd;
    tab_ = p.tab < 3 ? Tab(p.tab) : Tab::Clock;
  }
}

void TimerApp::service(uint32_t nowMs) {
  if (cd_.update(now64(nowMs))) {
    // Time's up: beep, and bring the countdown to the front with the screen
    // on, whatever was showing, even after a deep sleep (the timer woke it).
    alarm_ = true;
    alarmUntil_ = nowMs + SOUND_ALARM_MS;
    nextAlarmMs_ = nowMs;
    tab_ = Tab::Countdown;
    apps::manager::attention(this);
    if (root_) {
      showTab();
      ui::flash(root_, TIMER_COLOR);
    }
  }
  if (alarm_) {
    if (core::reached(nowMs, alarmUntil_)) {
      alarm_ = false;
    } else if (core::reached(nowMs, nextAlarmMs_)) {
      core::sound::play(core::sound::Pattern::Alarm);
      nextAlarmMs_ = nowMs + TIMER_ALARM_REPEAT_MS;
    }
  }
}

void TimerApp::stopAlarm() { alarm_ = false; }

void TimerApp::save() {
  const Saved s{uint16_t(cd_.seconds()), uint8_t(tab_)};
  core::store::save("timer", &s, sizeof s);
}

// A running countdown wakes the device when it ends; the alarm keeps the chip
// up until it's over (the screen is on for it anyway, unless turned off).
sleepplan::Need TimerApp::sleepNeed(uint64_t monoNow) const {
  return {alarm_, cd_.running() ? monoNow + cd_.remainingMs(monoNow) : 0};
}

// Sleep: both timers carry on (their clock counts through deep sleep).
// Power-off: they pause, and come back paused.
void TimerApp::onSleep(sleepplan::Kind kind) {
  const uint64_t now = now64(millis());
  stopAlarm();
  if (kind == sleepplan::Kind::PowerOff) {
    if (sw_.running()) sw_.toggle(now);
    if (cd_.running()) cd_.toggle(now);
  }
  const Parked p{sw_, cd_, uint8_t(tab_)};
  core::store::park("timer", &p, sizeof p);
}

// The power button silences the alarm, like a phone's.
void TimerApp::onScreen(bool on) {
  if (!on) stopAlarm();
}

bool TimerApp::onBack() {
  if (!alarm_) return false;
  stopAlarm();  // first Back silences the alarm; the overtime stays on screen
  return true;
}

// The clock tab, and any running timer, keep the screen on as a minimal view.
TimerApp::Show TimerApp::minimalShows() const {
  const bool cdActive = cd_.state() == State::Running || cd_.state() == State::Done;
  if (root_) {  // open: what the current tab shows, if it has something to show
    if (tab_ == Tab::Clock) return Show::Clock;
    if (tab_ == Tab::Stopwatch && sw_.running()) return Show::Stopwatch;
    if (tab_ == Tab::Countdown && cdActive) return Show::Countdown;
  }
  if (cdActive) return Show::Countdown;
  if (sw_.running()) return Show::Stopwatch;
  return Show::None;
}

power::Policy TimerApp::powerPolicy() const {
  return minimalShows() == Show::None ? power::Policy::Normal : power::Policy::LowPowerDisplay;
}
power::Policy TimerApp::backgroundPolicy() const { return powerPolicy(); }
uint8_t TimerApp::minimalBrightness() const { return TIMER_MINIMAL_BRIGHTNESS; }

// ---------------------------------------------------------------- full view

static lv_obj_t *bottomRow(lv_obj_t *parent) {
  lv_obj_t *row = ui::box(parent);
  lv_obj_set_size(row, LCD_WIDTH, ui::kBtn);
  lv_obj_set_pos(row, 0, LCD_HEIGHT - ui::kBarBottom - ui::kBtn);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 16, 0);
  return row;
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t cy) {
  lv_obj_t *l = ui::label(parent, font, color, "");
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, cy - lv_font_get_line_height(font) / 2);
  return l;
}

void TimerApp::onCreate(lv_obj_t *root) {
  using namespace ui;
  root_ = root;
  if (cd_.state() == State::Done) tab_ = Tab::Countdown;  // a finished countdown is why we're here

  for (auto &v : views_) {
    v = box(root);
    lv_obj_set_size(v, LCD_WIDTH, LCD_HEIGHT);
  }

  // Clock: hours over minutes.
  lv_obj_t *clock = views_[int(Tab::Clock)];
  hh_ = centred(clock, &font_digits_160, col::text, kHhY);
  mm_ = centred(clock, &font_digits_160, TIMER_COLOR, kMmY);
  date_ = centred(clock, &font_body, col::textDim, 398);

  // Stopwatch: mm:ss with tenths, start/pause and reset.
  lv_obj_t *sw = views_[int(Tab::Stopwatch)];
  lv_obj_t *group = box(sw);
  lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_align(group, LV_ALIGN_TOP_MID, 0, 176);
  swDigits_ = label(group, &font_digits_80, col::text, "");
  swTenths_ = label(group, &font_digits_44, col::textDim, "");
  lv_obj_set_style_pad_bottom(swTenths_, 3, 0);
  swSub_ = centred(sw, &font_caption, col::textDim, 266);
  lv_obj_t *tap = box(sw);  // the digits area toggles too
  lv_obj_set_size(tap, LCD_WIDTH, 200);
  lv_obj_set_pos(tap, 0, 110);
  lv_obj_set_clickable(tap, true);
  lv_obj_add_event_cb(tap, onSwToggle, LV_EVENT_CLICKED, this);
  lv_obj_t *swRow = bottomRow(sw);
  swToggle_ = pill(swRow, ICON_PLAY_ARROW, "Start", TIMER_COLOR);
  lv_obj_add_event_cb(swToggle_, onSwToggle, LV_EVENT_CLICKED, this);
  swReset_ = roundButton(swRow, ICON_RESTART_ALT, col::textDim);
  lv_obj_add_event_cb(swReset_, onSwReset, LV_EVENT_CLICKED, this);

  // Countdown, set: two wheels (drag, or tap above / below the middle).
  lv_obj_t *cd = views_[int(Tab::Countdown)];
  cdEdit_ = box(cd);
  lv_obj_set_size(cdEdit_, LCD_WIDTH, LCD_HEIGHT);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *col = box(cdEdit_);
    lv_obj_set_size(col, kColW, kColH);
    lv_obj_set_pos(col, i == 0 ? LCD_WIDTH / 2 - kColGap / 2 - kColW : LCD_WIDTH / 2 + kColGap / 2, kColY);
    lv_obj_set_clickable(col, true);
    lv_obj_add_event_cb(col, i == 0 ? onMinutes : onSeconds, LV_EVENT_ALL, this);
    lv_obj_align(label(col, &font_icon_28, col::textDim, ICON_EXPAND_LESS), LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *digits = label(col, &font_digits_80, col::text, "00");
    lv_obj_align(digits, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(label(col, &font_icon_28, col::textDim, ICON_EXPAND_MORE), LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_t *unit = label(cdEdit_, &font_caption, col::textDim, i == 0 ? "min" : "sec");
    lv_obj_align_to(unit, col, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    (i == 0 ? cdMin_ : cdSec_) = digits;
  }
  lv_obj_t *colon = label(cdEdit_, &font_digits_80, col::textDim, ":");
  lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, kColY + kColH / 2 - lv_font_get_line_height(&font_digits_80) / 2);

  // Countdown, running: remaining time in a ring.
  cdRun_ = box(cd);
  lv_obj_set_size(cdRun_, LCD_WIDTH, LCD_HEIGHT);
  cdRing_ = progressRing(cdRun_, kRing, 6);
  lv_obj_set_pos(cdRing_, (LCD_WIDTH - kRing) / 2, kRingY);
  cdDigits_ = centred(cdRun_, &font_digits_80, col::text, kRingY + kRing / 2 - 8);
  cdSub_ = centred(cdRun_, &font_caption, col::textDim, kRingY + kRing / 2 + 42);
  lv_obj_t *ringTap = box(cdRun_);
  lv_obj_set_size(ringTap, 220, 220);
  lv_obj_set_pos(ringTap, (LCD_WIDTH - 220) / 2, kRingY + 20);
  lv_obj_set_clickable(ringTap, true);
  lv_obj_add_event_cb(ringTap, onCdToggle, LV_EVENT_CLICKED, this);

  lv_obj_t *cdRow = bottomRow(cd);
  cdToggle_ = pill(cdRow, ICON_PLAY_ARROW, "Start", TIMER_COLOR);
  lv_obj_add_event_cb(cdToggle_, onCdToggle, LV_EVENT_CLICKED, this);
  cdReset_ = roundButton(cdRow, ICON_RESTART_ALT, col::textDim);
  lv_obj_add_event_cb(cdReset_, onCdReset, LV_EVENT_CLICKED, this);

  // Last, so it stays on top of the views.
  tabs_ = segmented(root, kTabs, 3, &font_caption, 0, onTab, this);
  lv_obj_align(tabs_, LV_ALIGN_TOP_MID, 0, kTabY);

  showTab();
}

void TimerApp::onDestroy() {
  root_ = tabs_ = hh_ = mm_ = date_ = nullptr;
  swDigits_ = swTenths_ = swSub_ = swToggle_ = swReset_ = nullptr;
  cdEdit_ = cdMin_ = cdSec_ = cdRun_ = cdRing_ = cdDigits_ = cdSub_ = cdToggle_ = cdReset_ = nullptr;
  for (auto &v : views_) v = nullptr;
}

void TimerApp::showTab() {
  if (!root_) return;
  for (int i = 0; i < 3; i++) lv_obj_set_hidden(views_[i], i != int(tab_));
  ui::setSegment(tabs_, int(tab_), ui::c(TIMER_COLOR), ui::c(ui::mixBlack(TIMER_COLOR, 14)));
  for (auto &k : lastKey_) k = UINT32_MAX;
  render(millis());
}

void TimerApp::onTick(uint32_t nowMs) {
  if (root_) render(nowMs);
}

void TimerApp::render(uint32_t nowMs) {
  switch (tab_) {
    case Tab::Clock: renderClock(); break;
    case Tab::Stopwatch: renderStopwatch(nowMs); break;
    case Tab::Countdown: renderCountdown(nowMs); break;
  }
}

void TimerApp::renderClock() {
  struct tm t;
  const bool valid = localNow(t);
  const uint32_t key = valid ? uint32_t(time(nullptr) / 60) : UINT32_MAX - 1;
  if (key == lastKey_[0]) return;
  lastKey_[0] = key;
  if (!valid) {
    ui::setText(hh_, "--");
    ui::setText(mm_, "--");
    ui::setText(date_, "Time not set: connect Wi-Fi");
    return;
  }
  ui::setTextFmt(hh_, "%02d", t.tm_hour);
  ui::setTextFmt(mm_, "%02d", t.tm_min);
  ui::setTextFmt(date_, "%s %d %s", kDays[t.tm_wday % 7], t.tm_mday, kMonths[t.tm_mon % 12]);
}

void TimerApp::renderStopwatch(uint32_t nowMs) {
  const uint64_t ms = sw_.elapsedMs(now64(nowMs));
  const uint32_t ds = uint32_t(ms / 100);
  const uint32_t key = ds * 2 + sw_.running();
  if (key == lastKey_[1]) return;
  lastKey_[1] = key;

  char buf[16];
  fmtTime(buf, sizeof buf, ds / 10);
  ui::setText(swDigits_, buf);
  lv_obj_set_hidden(swTenths_, ds >= 36000);  // h:mm:ss fills the width
  ui::setTextFmt(swTenths_, ".%lu", (unsigned long)(ds % 10));
  if (sw_.running()) {
    ui::setText(swSub_, "running");
    ui::setPillContent(swToggle_, ICON_PAUSE, "Pause");
  } else {
    ui::setText(swSub_, sw_.zero() ? "tap to start" : "paused");
    ui::setPillContent(swToggle_, ICON_PLAY_ARROW, sw_.zero() ? "Start" : "Resume");
  }
  lv_obj_set_hidden(swReset_, sw_.running() || sw_.zero());
}

void TimerApp::renderCountdown(uint32_t nowMs) {
  const uint64_t now = now64(nowMs);
  const State st = cd_.state();
  const uint64_t rem = cd_.remainingMs(now);
  const uint32_t sec = st == State::Done ? uint32_t(cd_.overMs(now) / 1000) : uint32_t((rem + 999) / 1000);
  const uint32_t key = (sec << 3) | uint32_t(st) << 1 | (cd_.seconds() == 0);
  if (key == lastKey_[2]) return;
  lastKey_[2] = key;

  lv_obj_set_hidden(cdEdit_, st != State::Idle);
  lv_obj_set_hidden(cdRun_, st == State::Idle);
  lv_obj_set_hidden(cdReset_, st != State::Running && st != State::Paused);
  if (st == State::Idle) {
    ui::setTextFmt(cdMin_, "%02lu", (unsigned long)(cd_.seconds() / 60));
    ui::setTextFmt(cdSec_, "%02lu", (unsigned long)(cd_.seconds() % 60));
    ui::setPillContent(cdToggle_, ICON_PLAY_ARROW, "Start");
    lv_obj_set_style_text_color(cdToggle_, ui::c(cd_.seconds() ? TIMER_COLOR : ui::col::textOff), 0);
    return;
  }

  char buf[16];
  const bool done = st == State::Done;
  fmtTime(buf, sizeof buf, sec, done ? "-" : "");
  ui::setText(cdDigits_, buf);
  if (lv_obj_get_style_text_font(cdDigits_, LV_PART_MAIN) != ringFont(buf)) {
    lv_obj_set_style_text_font(cdDigits_, ringFont(buf), 0);
    lv_obj_set_y(cdDigits_, kRingY + kRing / 2 - 8 - lv_font_get_line_height(ringFont(buf)) / 2);
  }
  const uint32_t color = done ? ui::col::warning : TIMER_COLOR;
  lv_obj_set_style_text_color(cdDigits_, ui::c(done ? ui::col::warning : ui::col::text), 0);
  lv_obj_set_style_arc_color(cdRing_, ui::c(color), LV_PART_INDICATOR);
  const uint64_t dur = uint64_t(cd_.seconds()) * 1000;
  lv_arc_set_value(cdRing_, done || !dur ? 1000 : int32_t(1000 - rem * 1000 / dur));
  lv_obj_set_style_text_color(cdToggle_, ui::c(color), 0);

  struct tm t;
  if (done) {
    ui::setText(cdSub_, "time's up");
    ui::setPillContent(cdToggle_, ICON_CANCEL, "Stop");
  } else if (st == State::Paused) {
    ui::setText(cdSub_, "paused");
    ui::setPillContent(cdToggle_, ICON_PLAY_ARROW, "Resume");
  } else {
    if (localNow(t)) {  // when it will end, by the wall clock
      const time_t end = time(nullptr) + time_t((rem + 999) / 1000);
      localtime_r(&end, &t);
      ui::setTextFmt(cdSub_, "ends %02d:%02d", t.tm_hour, t.tm_min);
    } else {
      ui::setText(cdSub_, "running");
    }
    ui::setPillContent(cdToggle_, ICON_PAUSE, "Pause");
  }
}

// ---------------------------------------------------------------- events

void TimerApp::onTab(lv_event_t *e) {
  auto *self = static_cast<TimerApp *>(lv_event_get_user_data(e));
  self->tab_ = Tab(ui::eventIndex(e));
  self->showTab();
  self->save();
}

void TimerApp::onSwToggle(lv_event_t *e) {
  auto *self = static_cast<TimerApp *>(lv_event_get_user_data(e));
  self->sw_.toggle(now64(millis()));
}

void TimerApp::onSwReset(lv_event_t *e) { static_cast<TimerApp *>(lv_event_get_user_data(e))->sw_.reset(); }

void TimerApp::onCdToggle(lv_event_t *e) {
  auto *self = static_cast<TimerApp *>(lv_event_get_user_data(e));
  if (self->cd_.state() == State::Idle) self->save();  // remember the time it was started with
  self->cd_.toggle(now64(millis()));
  self->stopAlarm();
}

void TimerApp::onCdReset(lv_event_t *e) {
  auto *self = static_cast<TimerApp *>(lv_event_get_user_data(e));
  self->cd_.reset();
  self->stopAlarm();
}

void TimerApp::onMinutes(lv_event_t *e) { static_cast<TimerApp *>(lv_event_get_user_data(e))->wheel(e, true); }
void TimerApp::onSeconds(lv_event_t *e) { static_cast<TimerApp *>(lv_event_get_user_data(e))->wheel(e, false); }

// A wheel column: drag up to count up, down to count down; or tap (hold to
// repeat) above or below the middle.
void TimerApp::wheel(lv_event_t *e, bool minutes) {
  if (cd_.state() != State::Idle) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p{0, 0};
  if (lv_indev_t *in = lv_indev_active()) lv_indev_get_point(in, &p);
  auto step = [&](int d) {
    if (minutes)
      cd_.stepMinutes(d);
    else
      cd_.stepSeconds(d);
  };
  if (code == LV_EVENT_PRESSED) {
    pressY_ = p.y;
    dragSteps_ = 0;
    dragged_ = false;
  } else if (code == LV_EVENT_PRESSING) {
    const int32_t d = pressY_ - p.y;
    if (abs(d) > kDragSlop) dragged_ = true;
    const int32_t steps = d / kWheelStepPx;
    if (dragged_ && steps != dragSteps_) {
      step(steps - dragSteps_);
      dragSteps_ = steps;
    }
  } else if ((code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) && !dragged_) {
    lv_area_t a;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_current_target(e)), &a);
    step(p.y < (a.y1 + a.y2) / 2 ? 1 : -1);
  } else if (code == LV_EVENT_RELEASED) {
    save();
  }
}

// ------------------------------------------------------------- minimal view

void TimerApp::enterMinimal(lv_obj_t *layer) {
  minLayer_ = layer;
  minShows_ = minimalShows();
  if (minShows_ == Show::Clock) {
    minA_ = ui::label(layer, &font_digits_160, ui::col::text, "");
    minB_ = ui::label(layer, &font_digits_160, TIMER_COLOR, "");
  } else {
    minA_ = ui::label(layer, &font_digits_80, TIMER_COLOR, "");
    minB_ = ui::label(layer, &font_caption, ui::col::textDim, "");
  }
  renderMinimal(millis());
}

void TimerApp::exitMinimal() {
  minLayer_ = minA_ = minB_ = nullptr;
  for (auto &k : lastKey_) k = UINT32_MAX;  // the full view may be stale after light sleep
}

void TimerApp::minimalTick(uint32_t nowMs) {
  if (minLayer_) renderMinimal(nowMs);
}

uint32_t TimerApp::minimalRefreshMs(uint32_t nowMs) const {
  const uint64_t now = now64(nowMs);
  switch (minShows_) {
    case Show::Clock: {  // the next wall-clock minute
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      return uint32_t(60000 - (tv.tv_sec % 60) * 1000 - tv.tv_usec / 1000) + 50;
    }
    case Show::Countdown:  // the next change of the displayed second
      if (cd_.state() == State::Done) return 1000 - uint32_t(cd_.overMs(now) % 1000) + 20;
      if (cd_.state() == State::Running) return uint32_t(cd_.remainingMs(now) % 1000) + 20;
      break;
    case Show::Stopwatch:
      if (sw_.running()) return 1000 - uint32_t(sw_.elapsedMs(now) % 1000) + 20;
      break;
    case Show::None: break;
  }
  return 60000;
}

// Sparse and dim, and it moves every minute (UI_DESIGN §6).
void TimerApp::renderMinimal(uint32_t nowMs) {
  const uint64_t now = now64(nowMs);
  char buf[16];
  if (minShows_ == Show::Clock) {
    struct tm t;
    const bool valid = localNow(t);
    const lv_point_t d = ui::drift(valid ? int32_t(time(nullptr) / 60) : 0, 40, 50);
    if (valid) {
      ui::setTextFmt(minA_, "%02d", t.tm_hour);
      ui::setTextFmt(minB_, "%02d", t.tm_min);
    } else {
      ui::setText(minA_, "--");
      ui::setText(minB_, "--");
    }
    const int32_t h = lv_font_get_line_height(&font_digits_160);
    lv_obj_align(minA_, LV_ALIGN_TOP_MID, d.x, kHhY - 14 - h / 2 + d.y);
    lv_obj_align(minB_, LV_ALIGN_TOP_MID, d.x, kMmY - 14 - h / 2 + d.y);
    return;
  }

  uint32_t sec = 0;
  const char *sign = "", *caption = "stopwatch";
  uint32_t color = TIMER_COLOR;
  if (minShows_ == Show::Countdown && cd_.state() == State::Done) {
    sec = uint32_t(cd_.overMs(now) / 1000);
    sign = "-";
    caption = "time's up";
    color = ui::col::warning;
  } else if (minShows_ == Show::Countdown) {
    sec = uint32_t((cd_.remainingMs(now) + 999) / 1000);
    caption = "countdown";
  } else {
    sec = uint32_t(sw_.elapsedMs(now) / 1000);
  }
  fmtTime(buf, sizeof buf, sec, sign);
  ui::setText(minA_, buf);
  lv_obj_set_style_text_color(minA_, ui::c(color), 0);
  ui::setText(minB_, caption);
  const lv_point_t d = ui::drift(int32_t(nowMs / 60000), sec >= 3600 ? 8 : 40, 110);
  const int32_t cy = LCD_HEIGHT / 2 + d.y;
  lv_obj_align(minA_, LV_ALIGN_TOP_MID, d.x, cy - lv_font_get_line_height(&font_digits_80) / 2);
  lv_obj_align(minB_, LV_ALIGN_TOP_MID, d.x, cy + 40);
}

}  // namespace apps
