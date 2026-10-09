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
#include "net.h"
#include "pmu.h"
#include "touch.h"
#include "ui/power_anim.h"

namespace core::power {

using ::power::Policy;
using ::power::Profile;
using ::power::Stage;

static const char *TAG = "power";

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
  if (h.prepareSleep) h.prepareSleep();
  net::shutdown();
  display::enterSleep();
  touch::sleep();
  // BOOT (GPIO0, RTC-capable) wakes the chip; keep its pull-up alive in sleep.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BOOT_BUTTON, ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  esp_deep_sleep_start();
}

static void apply(Stage s, Stage old) {
  if (old == Stage::Minimal && s != Stage::Minimal && h.exitMinimal) h.exitMinimal();
  if (panelOff && s != Stage::Black) {
    display::setPanelOn(true);
    display::pauseRendering(false);
    panelOff = false;
  }
  display::setRefreshPeriod(s == Stage::Dimmed ? UI_FRAME_MS_DIM : UI_FRAME_MS);
  if (s == Stage::Minimal && old != Stage::Minimal) {
    net::shutdown();  // light sleep and Wi-Fi don't mix; nothing needs it here
    if (h.enterMinimal) h.enterMinimal();
  }
  display::fadeTo(::power::brightnessFor(s, prof, kLevels), BRIGHTNESS_FADE_MS);
  ESP_LOGD(TAG, "stage %s", ::power::stageName(s));
}

// Minimal view: render the sparse frame, then light-sleep until the next
// redraw. The panel keeps showing the frame from its own RAM meanwhile.
static void minimalCycle(uint32_t now) {
  if (h.minimalTick) h.minimalTick(now);
  lv_refr_now(display::lv());
  const uint32_t t0 = millis();
  while (!display::flushIdle() && millis() - t0 < 100) delay(1);
  if (buttons::bootHeld() || display::brightness() != BRIGHTNESS_MINIMAL) return;  // still fading

  uint32_t ms = h.minimalRefreshMs ? h.minimalRefreshMs(now) : 1000;
  ms = constrain(ms, 200u, 60000u);
  esp_sleep_enable_timer_wakeup(uint64_t(ms) * 1000);
  gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(gpio_num_t(PIN_TOUCH_INT), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  const bool byInput = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(GPIO_NUM_0);
  gpio_wakeup_disable(gpio_num_t(PIN_TOUCH_INT));
  gpio_set_intr_type(gpio_num_t(PIN_TOUCH_INT), GPIO_INTR_NEGEDGE);  // back to the touch ISR's edge
  // The ISR may have missed the edge while asleep; the touch/button that
  // woke us is then handled (and swallowed) through onPress().
  if (byInput) touch::kick();
}

void begin(const Hooks &hooks) {
  h = hooks;
  lastInputMs = millis();
  prof = pmu::onCharger() ? Profile::Charger : Profile::Battery;
  apply(Stage::Active, Stage::Active);
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
    apply(Stage::Active, st);
    st = Stage::Active;
  }

  const Stage s = ::power::stageFor(now - lastInputMs, prof, h.policy ? h.policy() : Policy::Normal, timings());
  if (s != st) {
    const Stage old = st;
    st = s;
    if (s == Stage::Sleep) {
      goDeepSleep();
      return;
    }
    apply(s, old);
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
  if (st != Stage::Active) {
    apply(Stage::Active, st);
    st = Stage::Active;
  }
  sleepPhase = SleepPhase::Animating;
  ui::power_anim::off([]() {
    sleepPhase = SleepPhase::WaitRelease;
    releaseWaitStart = millis();
  });
}

Stage stage() { return st; }
Profile profile() { return prof; }
uint32_t idleMs() { return millis() - lastInputMs; }

}  // namespace core::power
