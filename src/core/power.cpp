#include "power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>

#include "board.h"
#include "buttons.h"
#include "config.h"
#include "display.h"
#include "imu.h"
#include "net.h"
#include "ota.h"
#include "pmu.h"
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
static constexpr uint32_t kFlushWaitMs = 100;  // longest wait for the minimal frame's DMA before sleeping

static constexpr ::power::Levels kLevels = {BRIGHTNESS_MAX_BATTERY, BRIGHTNESS_MAX_CHARGER, BRIGHTNESS_DIM_BATTERY,
                                            BRIGHTNESS_DIM_CHARGER, BRIGHTNESS_MINIMAL};

enum class SleepPhase : uint8_t { None, Animating, WaitRelease };

static Hooks h;
static uint32_t lastInputMs;
static Stage st = Stage::Active;
static Profile prof = Profile::Battery;
static SleepPhase sleepPhase = SleepPhase::None;
static uint32_t releaseWaitStart;
static bool panelOff;
static bool noMinimal;         // the minimal stage was refused: show Black instead
static uint32_t minimalDueMs;  // next minimal-view redraw

static ::power::Timings timings() {
  ::power::Timings t = {BAT_IDLE_DIM_MS, BAT_IDLE_BLACK_MS, BAT_IDLE_SLEEP_MS, CHG_IDLE_DIM_MS, CHG_IDLE_BLACK_MS};
  const uint32_t dim = h.idleDimMs ? h.idleDimMs() : 0;
  if (dim) {  // an app wants more time before dimming: shift the battery timeline
    t.batBlackMs = dim + (BAT_IDLE_BLACK_MS - BAT_IDLE_DIM_MS);
    t.batSleepMs = dim + (BAT_IDLE_SLEEP_MS - BAT_IDLE_DIM_MS);
    t.batDimMs = dim;
    if (t.chgDimMs < dim) t.chgDimMs = dim;
  }
  return t;
}

static void goDeepSleep() {
  ESP_LOGI(TAG, "deep sleep");
  // Getting here on purpose means the running image works: confirm it, or the
  // wake (a reboot) would roll a fresh OTA image back.
  ota::confirmIfPending();
  if (h.prepareSleep) h.prepareSleep();
  net::shutdown();
  imu::enable(false);  // Level may have left it running
  // Speaker amp off, and held off: GPIO46 isn't an RTC pin and would float.
  digitalWrite(PIN_PA_EN, LOW);
  gpio_hold_en(gpio_num_t(PIN_PA_EN));
  gpio_deep_sleep_hold_en();
  display::enterSleep();
  touch::sleep();
  // BOOT (GPIO0, RTC-capable) wakes the chip; keep its pull-up alive in sleep.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(kBootPin);
  rtc_gpio_pulldown_dis(kBootPin);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BOOT_BUTTON, ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  esp_deep_sleep_start();
}

// Switches to stage `s` and returns the stage actually entered: Black when
// Minimal was asked for but no app provides a minimal view.
static Stage apply(Stage s, Stage old) {
  if (old == Stage::Minimal && s != Stage::Minimal) {
    if (h.exitMinimal) h.exitMinimal();
    net::suspend(false);
  }
  if (s == Stage::Minimal && old != Stage::Minimal) {
    if (h.enterMinimal && h.enterMinimal()) {
      net::suspend(true);       // light sleep and Wi-Fi don't mix; nothing needs it here
      minimalDueMs = millis();  // draw right away
    } else {
      noMinimal = true;
      s = Stage::Black;
    }
  }
  if (panelOff && s != Stage::Black) {
    display::setPanelOn(true);
    display::pauseRendering(false);
    panelOff = false;
  }
  display::setRefreshPeriod(s == Stage::Dimmed ? UI_FRAME_MS_DIM : UI_FRAME_MS);
  display::fadeTo(::power::brightnessFor(s, prof, kLevels), BRIGHTNESS_FADE_MS);
  ESP_LOGD(TAG, "stage %s", ::power::stageName(s));
  return s;
}

// Minimal view: render the sparse frame when it's due, then light-sleep. The
// panel keeps showing the frame from its own RAM meanwhile.
// PWR can't wake the chip (the AXP2101 IRQ only reaches the TCA9554, not a
// GPIO), so a sleep lasts at most MINIMAL_PWR_POLL_MS; the main loop then
// polls the PMU and a PWR press ends the minimal view within ~1 s. A wake
// that isn't a redraw only runs one loop pass and that I2C poll.
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
  if (buttons::bootHeld() || display::brightness() != BRIGHTNESS_MINIMAL) return;  // still fading

  const int32_t left = int32_t(minimalDueMs - millis());
  if (left <= 0) return;  // redraw on the next pass
  const uint32_t ms = left < MINIMAL_PWR_POLL_MS ? uint32_t(left) : uint32_t(MINIMAL_PWR_POLL_MS);
  esp_sleep_enable_timer_wakeup(uint64_t(ms) * 1000);
  // gpio_wakeup_enable() switches the pin to level interrupts; with the touch
  // ISR attached that would fire continuously while INT is low after the
  // wake. Keep the ISR off until the edge trigger is back.
  gpio_intr_disable(kTouchIntPin);
  gpio_wakeup_enable(kBootPin, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(kTouchIntPin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  const bool byInput = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(kBootPin);
  gpio_wakeup_disable(kTouchIntPin);
  gpio_set_intr_type(kTouchIntPin, GPIO_INTR_NEGEDGE);  // back to the touch ISR's edge
  gpio_intr_enable(kTouchIntPin);
  // The ISR may have missed the edge while asleep; the touch/button that
  // woke us is then handled (and swallowed) through onPress().
  if (byInput) touch::kick();
}

void begin(const Hooks &hooks) {
  h = hooks;
  lastInputMs = millis();
  prof = pmu::onCharger() ? Profile::Charger : Profile::Battery;
  st = apply(Stage::Active, Stage::Active);
}

void update() {
  const uint32_t now = millis();
  if (sleepPhase == SleepPhase::Animating) return;
  if (sleepPhase == SleepPhase::WaitRelease) {
    // A held BOOT would wake the chip again instantly (it's the wake pin).
    if (!buttons::bootHeld() || now - releaseWaitStart > RELEASE_WAIT_TIMEOUT_MS) goDeepSleep();
    return;
  }

  const Profile p = pmu::onCharger() ? Profile::Charger : Profile::Battery;
  if (p != prof) {
    prof = p;
    lastInputMs = now;  // plugging in or out counts as activity
    st = apply(Stage::Active, st);
  }

  Stage s = ::power::stageFor(now - lastInputMs, prof, h.policy ? h.policy() : Policy::Normal, timings());
  if (s != Stage::Minimal)
    noMinimal = false;
  else if (noMinimal)
    s = Stage::Black;  // don't ask for a minimal view again every pass
  if (s != st) {
    if (s == Stage::Sleep) {
      st = s;
      goDeepSleep();
      return;
    }
    st = apply(s, st);
  }

  // Black: once faded out, switch the panel off (every pixel off) and stop rendering.
  if (st == Stage::Black && !panelOff && display::brightness() == 0) {
    display::setPanelOn(false);
    display::pauseRendering(true);
    panelOff = true;
  }
  if (st == Stage::Minimal) minimalCycle(now);
}

bool onPress() {
  const bool wasActive = st == Stage::Active && sleepPhase == SleepPhase::None;
  lastInputMs = millis();
  return wasActive;
}

void activity() { lastInputMs = millis(); }

void requestSleep() {
  if (sleepPhase != SleepPhase::None) return;
  ESP_LOGI(TAG, "long press: powering off");
  if (st != Stage::Active) st = apply(Stage::Active, st);
  sleepPhase = SleepPhase::Animating;
  ui::power_anim::off([]() {
    sleepPhase = SleepPhase::WaitRelease;
    releaseWaitStart = millis();
  });
}

Stage stage() { return st; }

}  // namespace core::power
