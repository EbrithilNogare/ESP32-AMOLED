#include "power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <time.h>

#include "board.h"
#include "board_init.h"
#include "buttons.h"
#include "config.h"
#include "display.h"
#include "imu.h"
#include "net.h"
#include "ota.h"
#include "pmu.h"
#include "rtc_clock.h"
#include "settings.h"
#include "sound.h"
#include "store.h"
#include "ticks.h"
#include "touch.h"
#include "ui/power_anim.h"

namespace core::power {

using ::power::Policy;
using ::power::Profile;
using ::power::Stage;

[[maybe_unused]] static const char *TAG = "power";

static constexpr gpio_num_t kBootPin = gpio_num_t(PIN_BOOT_BUTTON);
static constexpr gpio_num_t kTouchIntPin = gpio_num_t(PIN_TOUCH_INT);
static constexpr uint32_t kFlushWaitMs = 100;   // longest wait for the minimal frame's DMA before sleeping
static constexpr uint32_t kSoundWaitMs = 1000;  // longest wait for noise to fade out (and the codec to power down)
static constexpr uint32_t kActiveCpuMhz = 240;
static constexpr sleepplan::Config kPlan = {SLEEP_MIN_DEEP_MS, SLEEP_WAKE_LEAD_MS, SLEEP_POLL_MS};

enum class Phase : uint8_t { None, Animating, WaitRelease };

static Hooks h;

// Active = the user's brightness (both profiles); dimmed stays clearly
// darker than that, whatever the user picked. The minimal view's level comes
// from the app showing it (the Timer clock is brighter than the default);
// brightnessFor() caps every level at the user's.
static ::power::Levels levels() {
  const uint8_t user = settings::brightnessLevel();
  const uint8_t half = user / 2;
  const uint8_t minimal = h.minimalLevel ? h.minimalLevel() : 0;
  return {user, user, uint8_t(half < BRIGHTNESS_DIM_BATTERY ? half : BRIGHTNESS_DIM_BATTERY),
          uint8_t(half < BRIGHTNESS_DIM_CHARGER ? half : BRIGHTNESS_DIM_CHARGER),
          minimal ? minimal : uint8_t(BRIGHTNESS_MINIMAL)};
}
static uint32_t lastInputMs;
static Stage st = Stage::Active;
static Profile prof = Profile::Battery;
static Phase phase = Phase::None;
static uint32_t releaseWaitStart;
static uint32_t offSinceMs;    // when the screen went off
static bool silent;            // a timer wake: nobody has asked for the screen yet
static bool panelDown;         // Off and faded out: panel and touch asleep, rendering paused
static uint32_t minimalDueMs;  // next minimal-view redraw

static ::power::Timings timings() {
  ::power::Timings t = {BAT_IDLE_DIM_MS, BAT_IDLE_OFF_MS, CHG_IDLE_DIM_MS, CHG_IDLE_OFF_MS};
  const uint32_t dim = h.idleDimMs ? h.idleDimMs() : 0;
  if (dim) {  // an app wants more time before dimming: shift the battery timeline
    t.batOffMs = dim + (BAT_IDLE_OFF_MS - BAT_IDLE_DIM_MS);
    t.batDimMs = dim;
    if (t.chgDimMs < dim) t.chgDimMs = dim;
  }
  return t;
}

Boot bootCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1: return Boot::Button;
    case ESP_SLEEP_WAKEUP_TIMER: return Boot::Timer;
    default: return Boot::Cold;
  }
}

// Deep sleep: a wake is a reboot, which restores the apps (§8.3). `timerMs`
// 0 = only the power button wakes.
static void goDeepSleep(sleepplan::Kind kind, uint64_t timerMs) {
  ESP_LOGI(TAG, "%s, timer %llu ms", kind == sleepplan::Kind::PowerOff ? "power off" : "deep sleep",
           (unsigned long long)timerMs);
  // Getting here on purpose means the running image works: confirm it, or the
  // wake (a reboot) would roll a fresh OTA image back.
  ota::confirmIfPending();
  if (h.prepareSleep) h.prepareSleep(kind);  // apps park their state, stop noise
  for (const uint32_t t0 = millis(); sound::busy() && millis() - t0 < kSoundWaitMs;) delay(10);
  pmu::refreshLevel();
  sleeplog::enter(store::rtc().sleep, time(nullptr), pmu::batteryPercent(), pmu::batteryMillivolts(), pmu::onCharger());
  store::rtc().sleepKind = uint8_t(kind);
  clock::noteSleep(mono(millis()));
  store::rtcCommit();
  net::shutdown();
  imu::enable(false);  // powered down whatever left it on
  // Speaker amp off, and held off: GPIO46 isn't an RTC pin and would float.
  digitalWrite(PIN_PA_EN, LOW);
  gpio_hold_en(gpio_num_t(PIN_PA_EN));
  gpio_deep_sleep_hold_en();
  display::enterSleep();
  touch::sleep();
  pmu::prepareSleep();
  // BOOT (GPIO0, RTC-capable) wakes the chip; keep its pull-up alive in sleep.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(kBootPin);
  rtc_gpio_pulldown_dis(kBootPin);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BOOT_BUTTON, ESP_EXT1_WAKEUP_ANY_LOW);
  if (timerMs) esp_sleep_enable_timer_wakeup(timerMs * 1000);
  Serial.flush();
  esp_deep_sleep_start();
}

// Switches between the screen-on stages (Active, Dimmed, Minimal) and returns
// the stage actually entered: Off when Minimal was asked for but no app
// provides a minimal view (the caller then turns the screen off).
static Stage apply(Stage s, Stage old) {
  if (old == Stage::Minimal && s != Stage::Minimal) {
    if (h.exitMinimal) h.exitMinimal();
    net::suspend(false);
  }
  if (s == Stage::Minimal && old != Stage::Minimal) {
    if (!h.enterMinimal || !h.enterMinimal()) return Stage::Off;
    net::suspend(true);       // light sleep and Wi-Fi don't mix; nothing needs it here
    minimalDueMs = millis();  // draw right away
  }
  display::setRefreshPeriod(s == Stage::Dimmed ? UI_FRAME_MS_DIM : UI_FRAME_MS);
  display::fadeTo(::power::brightnessFor(s, prof, levels()), BRIGHTNESS_FADE_MS);
  ESP_LOGD(TAG, "stage %s", ::power::stageName(s));
  return s;
}

// The screen goes off: fade out (instant after the power-off animation, which
// already ends black); doze() then puts panel and touch to sleep.
static void screenOff(bool instant) {
  if (st == Stage::Minimal && h.exitMinimal) h.exitMinimal();
  st = Stage::Off;
  offSinceMs = millis();
  net::suspend(true);  // nothing needs Wi-Fi with the screen off
  display::fadeTo(0, instant ? 0 : BRIGHTNESS_FADE_MS);
  if (h.screenChanged) h.screenChanged(false);
  ESP_LOGD(TAG, "screen off");
}

static void lightSleep(uint32_t ms, bool touchWakes) {
  esp_sleep_enable_timer_wakeup(uint64_t(ms) * 1000);
  // gpio_wakeup_enable() switches the pin to level interrupts; with the touch
  // ISR attached that would fire continuously while INT is low after the
  // wake. Keep the ISR off until the edge trigger is back.
  if (touchWakes) {
    gpio_intr_disable(kTouchIntPin);
    gpio_wakeup_enable(kTouchIntPin, GPIO_INTR_LOW_LEVEL);
  }
  gpio_wakeup_enable(kBootPin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  const bool byInput = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(kBootPin);
  if (touchWakes) {
    gpio_wakeup_disable(kTouchIntPin);
    gpio_set_intr_type(kTouchIntPin, GPIO_INTR_NEGEDGE);  // back to the touch ISR's edge
    gpio_intr_enable(kTouchIntPin);
    // The ISR may have missed the edge while asleep; the touch that woke us
    // is then handled (and swallowed) through onPress().
    if (byInput) touch::kick();
  }
}

// Off: once faded out, panel and touch go to sleep; then either deep sleep
// (never returns) or doze: light sleep until the next app deadline, the power
// button or the next charger check. While sound plays the CPU stays up
// (light sleep would stop the I2S stream), just at a lower clock.
static void doze(uint32_t now) {
  if (!panelDown) {
    if (display::brightness() != 0) return;  // still fading
    display::pauseRendering(true);
    display::enterSleep();
    touch::sleep();
    setCpuFrequencyMhz(DOZE_CPU_MHZ);
    panelDown = true;
  }
  // A timer wake: let the apps run a few passes first, one may want the screen.
  if (silent && now - offSinceMs < SLEEP_SILENT_WAKE_MS) return;

  const uint64_t m = mono(now);
  const sleepplan::Need need = h.sleepNeed ? h.sleepNeed(m) : sleepplan::Need{false, 0};
  const sleepplan::Plan plan = sleepplan::decide(need, m, prof == Profile::Charger, sleepplan::Kind::Sleep, kPlan);
  if (plan.mode == sleepplan::Mode::Deep) goDeepSleep(sleepplan::Kind::Sleep, plan.ms);

  if (sound::busy() || buttons::bootHeld() || plan.ms == 0) return;
  lightSleep(uint32_t(plan.ms), false);  // touch is asleep: only BOOT and the timer wake
}

// Minimal view: render the sparse frame when it's due, then light-sleep. The
// panel keeps showing the frame from its own RAM meanwhile. A sleep lasts at
// most SLEEP_POLL_MS so the charger and PWR are still polled now and then;
// touch and the power button wake it at once. While sound plays (Noise, a
// beep) the CPU doesn't light-sleep at all.
static void minimalCycle(uint32_t now) {
  if (reached(now, minimalDueMs)) {
    if (h.minimalTick) h.minimalTick(now);
    lv_refr_now(display::lv());
    const uint32_t t0 = millis();
    while (!display::flushIdle() && millis() - t0 < kFlushWaitMs) delay(1);
    uint32_t ms = h.minimalRefreshMs ? h.minimalRefreshMs(now) : MINIMAL_REFRESH_DEFAULT_MS;
    ms = constrain(ms, uint32_t(MINIMAL_REFRESH_MIN_MS), uint32_t(MINIMAL_REFRESH_MAX_MS));
    minimalDueMs = now + ms;
  }
  if (buttons::bootHeld() || display::brightness() != ::power::brightnessFor(Stage::Minimal, prof, levels()))
    return;                   // still fading
  if (sound::busy()) return;  // light sleep would stop the I2S stream: stay awake, just idle

  const int32_t left = int32_t(minimalDueMs - millis());
  if (left <= 0) return;  // redraw on the next pass
  lightSleep(left < SLEEP_POLL_MS ? uint32_t(left) : uint32_t(SLEEP_POLL_MS), true);
}

// After a wake: how much the battery lost while asleep (ARCHITECTURE §8.6).
static void noteWake() {
  sleeplog::Log &log = store::rtc().sleep;
  if (!sleeplog::wake(log, time(nullptr), pmu::batteryPercent(), pmu::batteryMillivolts(), pmu::onCharger())) return;
  [[maybe_unused]] const sleeplog::Sleep &s = log.last;
  const sleeplog::Estimate e = sleeplog::daysPerCharge(log);
  ESP_LOGI(TAG, "slept %lu h %02lu min: %d -> %d %% (%u -> %u mV); since the last charge %lu h asleep, -%d %%",
           (unsigned long)(s.seconds / 3600), (unsigned long)(s.seconds / 60 % 60), s.fromPct, s.toPct, s.fromMv,
           s.toMv, (unsigned long)(log.totalS / 3600), log.totalLost);
  if (e.days) ESP_LOGI(TAG, "a charge lasts %s%lu days asleep", e.atLeast ? "over " : "", (unsigned long)e.days);
}

const sleeplog::Log &sleepLog() { return store::rtc().sleep; }

void begin(const Hooks &hooks, Boot boot) {
  h = hooks;
  noteWake();
  lastInputMs = millis();
  prof = pmu::onCharger() ? Profile::Charger : Profile::Battery;
  if (boot == Boot::Timer) {  // main.cpp left the panel asleep
    silent = true;
    st = Stage::Off;
    offSinceMs = millis();
    panelDown = false;
    net::suspend(true);
    display::pauseRendering(true);
    if (h.screenChanged) h.screenChanged(false);
    return;
  }
  st = apply(Stage::Active, Stage::Active);
}

void wake() {
  lastInputMs = millis();
  if (st != Stage::Off || phase != Phase::None) return;
  ESP_LOGD(TAG, "screen on%s", silent ? " (after a timer wake)" : "");
  setCpuFrequencyMhz(kActiveCpuMhz);
  if (!display::panelAwake()) {
    board::resetPanelAndTouch();  // the touch controller only leaves its sleep through a reset
    display::wakePanel();
    touch::wake();
  }
  display::pauseRendering(false);
  panelDown = false;
  silent = false;
  net::suspend(false);
  st = Stage::Active;
  if (h.screenChanged) h.screenChanged(true);
  ui::power_anim::on();
  lv_refr_now(display::lv());  // a picture in the panel before the light comes up
  st = apply(Stage::Active, Stage::Off);
}

void update() {
  const uint32_t now = millis();
  if (phase == Phase::Animating) return;
  if (phase == Phase::WaitRelease) {
    // A held BOOT would wake the chip again instantly (it's the wake pin).
    if (!buttons::bootHeld() || now - releaseWaitStart > RELEASE_WAIT_TIMEOUT_MS)
      goDeepSleep(sleepplan::Kind::PowerOff, 0);
    return;
  }

  const Profile p = pmu::onCharger() ? Profile::Charger : Profile::Battery;
  if (p != prof) {
    prof = p;
    if (st != Stage::Off) {  // plugging in or out counts as activity while the screen is on
      lastInputMs = now;
      st = apply(Stage::Active, st);
    }
  }

  if (st == Stage::Off) {
    doze(now);
    return;
  }
  Stage s = ::power::stageFor(now - lastInputMs, prof, h.policy ? h.policy() : Policy::Normal, timings());
  if (s != st && s != Stage::Off) s = apply(s, st);
  if (s == Stage::Off) {
    screenOff(false);
    return;
  }
  st = s;
  if (st == Stage::Minimal) minimalCycle(now);
}

bool onPress() {
  if (phase != Phase::None || st == Stage::Off) return false;
  const bool wasActive = st == Stage::Active;
  lastInputMs = millis();
  return wasActive;
}

bool onPowerPress() {
  if (phase != Phase::None) return false;
  if (st == Stage::Off) {
    wake();
    return false;
  }
  if (st == Stage::Minimal) {  // like a phone's always-on display: the button brings the screen back
    lastInputMs = millis();
    return false;
  }
  return true;
}

void applyBrightness() {
  if (st == Stage::Active && phase == Phase::None) display::setBrightness(::power::brightnessFor(st, prof, levels()));
}

void activity() {
  if (st != Stage::Off) lastInputMs = millis();
}

void sleepNow() {
  if (phase != Phase::None || st == Stage::Off) return;
  ESP_LOGI(TAG, "power button: screen off");
  if (st == Stage::Minimal) st = apply(Stage::Active, st);
  phase = Phase::Animating;
  ui::power_anim::off([]() {
    phase = Phase::None;
    screenOff(true);
  });
}

void powerOff() {
  if (phase != Phase::None || st == Stage::Off) return;
  ESP_LOGI(TAG, "power button held: powering off");
  if (st != Stage::Active) st = apply(Stage::Active, st);
  phase = Phase::Animating;
  ui::power_anim::shutdown([]() {
    phase = Phase::WaitRelease;
    releaseWaitStart = millis();
  });
}

bool dozing() { return st == Stage::Off && panelDown; }

Stage stage() { return st; }

}  // namespace core::power
