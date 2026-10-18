#pragma once
#include <timer_core.h>

#include "apps/app.h"

namespace apps {

// Timer (docs/apps/timer.md): three tabs. Clock: hours over minutes, big,
// for presenting; the screen never goes off, it drops to a dim minimal view
// that moves every minute. Stopwatch, and a countdown set up to 99:59. The
// logic is lib/TimerCore; both timers keep running while other apps are open
// and through a sleep (their clock is core::mono()): a running countdown
// wakes the device when it ends.
class TimerApp : public App {
 public:
  enum class Tab : uint8_t { Clock, Stopwatch, Countdown };

  const AppInfo &info() const override;
  void onCreate(lv_obj_t *root) override;
  void onDestroy() override;
  void onTick(uint32_t nowMs) override;
  void service(uint32_t nowMs) override;
  bool onBack() override;

  power::Policy powerPolicy() const override;
  power::Policy backgroundPolicy() const override;
  void enterMinimal(lv_obj_t *layer) override;
  void exitMinimal() override;
  void minimalTick(uint32_t nowMs) override;
  uint32_t minimalRefreshMs(uint32_t nowMs) const override;
  uint8_t minimalBrightness() const override;
  sleepplan::Need sleepNeed(uint64_t monoNow) const override;
  void onSleep(sleepplan::Kind kind) override;
  void onWake(Wake wake) override;
  void onScreen(bool on) override;

  Tab tab() const { return tab_; }
  const timer::Stopwatch &stopwatch() const { return sw_; }
  const timer::Countdown &countdown() const { return cd_; }
  bool alarming() const { return alarm_; }

 private:
  enum class Show : uint8_t { None, Clock, Stopwatch, Countdown };  // what a minimal view shows
  struct Saved {                                                    // NVS: the settings
    uint16_t countdownS;
    uint8_t tab;
  };
  struct Parked {  // RTC memory: the running state, through a sleep
    timer::Stopwatch sw;
    timer::Countdown cd;
    uint8_t tab;
  };

  Show minimalShows() const;
  void save();
  void showTab();
  void render(uint32_t nowMs);
  void renderClock();
  void renderStopwatch(uint32_t nowMs);
  void renderCountdown(uint32_t nowMs);
  void renderMinimal(uint32_t nowMs);
  void stopAlarm();
  void wheel(lv_event_t *e, bool minutes);

  static void onTab(lv_event_t *e);
  static void onSwToggle(lv_event_t *e);
  static void onSwReset(lv_event_t *e);
  static void onCdToggle(lv_event_t *e);
  static void onCdReset(lv_event_t *e);
  static void onMinutes(lv_event_t *e);
  static void onSeconds(lv_event_t *e);

  timer::Stopwatch sw_;
  timer::Countdown cd_;
  Tab tab_ = Tab::Clock;
  bool alarm_ = false;  // a finished countdown is beeping
  uint32_t alarmUntil_ = 0, nextAlarmMs_ = 0;
  int32_t pressY_ = 0, dragSteps_ = 0;
  bool dragged_ = false;
  uint32_t lastKey_[3] = {};  // per tab: what's on screen, to skip identical redraws

  lv_obj_t *root_ = nullptr, *tabs_ = nullptr, *views_[3] = {};
  lv_obj_t *hh_ = nullptr, *mm_ = nullptr, *date_ = nullptr;               // clock
  lv_obj_t *swDigits_ = nullptr, *swTenths_ = nullptr, *swSub_ = nullptr;  // stopwatch
  lv_obj_t *swToggle_ = nullptr, *swReset_ = nullptr;
  lv_obj_t *cdEdit_ = nullptr, *cdMin_ = nullptr, *cdSec_ = nullptr;                        // countdown: set
  lv_obj_t *cdRun_ = nullptr, *cdRing_ = nullptr, *cdDigits_ = nullptr, *cdSub_ = nullptr;  // running
  lv_obj_t *cdToggle_ = nullptr, *cdReset_ = nullptr;

  lv_obj_t *minLayer_ = nullptr, *minA_ = nullptr, *minB_ = nullptr;
  Show minShows_ = Show::None;
};

}  // namespace apps
