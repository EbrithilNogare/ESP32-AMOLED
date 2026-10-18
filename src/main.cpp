// Wiring only (AGENTS.md): bring-up order in setup(), servicing in loop().
// The design is docs/ARCHITECTURE.md.
#include <Arduino.h>
#include <esp_log.h>
#include <lvgl.h>

#include "apps/app_manager.h"
#include "apps/bot_face/bot_face_app.h"
#include "apps/dice/dice_app.h"
#include "apps/espresso/espresso_app.h"
#include "apps/home_assistant/ha_app.h"
#include "apps/level/level_app.h"
#include "apps/menu/menu_app.h"
#include "apps/noise/noise_app.h"
#include "apps/pomodoro/pomodoro_app.h"
#include "apps/system/system_app.h"
#include "apps/timer/timer_app.h"
#include "apps/wifi/wifi_app.h"
#include "config.h"
#include "core/board_init.h"
#include "core/buttons.h"
#include "core/display.h"
#include "core/events.h"
#include "core/imu.h"
#include "core/net.h"
#include "core/ota.h"
#include "core/pmu.h"
#include "core/power.h"
#include "core/rtc_clock.h"
#include "core/settings.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/ticks.h"
#include "core/touch.h"
#include "ui/power_anim.h"
#include "ui/theme.h"
#include "version.h"

[[maybe_unused]] static const char *TAG = "main";

static apps::WifiApp wifiApp;  // opened from the quick settings, not listed in the menu
static apps::MenuApp menuApp(&wifiApp);
static apps::HomeAssistantApp haApp;
static apps::BotFaceApp botApp([]() { return core::power::stage() == power::Stage::Dimmed; });
static apps::PomodoroApp pomodoroApp;
static apps::EspressoApp espressoApp;
static apps::DiceApp diceApp;
static apps::LevelApp levelApp;
static apps::TimerApp timerApp;
static apps::NoiseApp noiseApp;
static apps::SystemApp systemApp;

static void dispatch(const core::Event &e) {
  using core::Ev;
  switch (e.type) {
    case Ev::Back: apps::manager::back(); break;
    case Ev::Home: apps::manager::home(); break;
    case Ev::Power:  // never mid-OTA
      if (!apps::manager::current()->blocksNavigation()) core::power::sleepNow();
      break;
    case Ev::PowerOff:
      if (!apps::manager::current()->blocksNavigation()) core::power::powerOff();
      break;
    case Ev::HaResult:
      haApp.onResult(int(e.a), apps::ha_client::stateOf(e.b), apps::ha_client::resultOf(e.b),
                     apps::ha_client::isToggle(e.b), apps::ha_client::generationOf(e.b));
      break;
    case Ev::HaLights: haApp.onLights(ha::Result(e.b)); break;
    case Ev::WifiChanged: haApp.onWifiChanged(); break;
    case Ev::TimeSynced: core::clock::onNtpSynced(); break;
  }
}

// What the apps are told about this boot: after a deep sleep, whether it
// was a sleep (carry on) or a power-off (paused).
static apps::Wake wakeFor(core::power::Boot boot) {
  if (boot == core::power::Boot::Cold || !core::store::rtcValid()) return apps::Wake::Cold;
  return core::store::rtc().sleepKind == uint8_t(sleepplan::Kind::PowerOff) ? apps::Wake::FromPowerOff
                                                                            : apps::Wake::FromSleep;
}

void setup() {
  const core::power::Boot boot = core::power::bootCause();
  const bool silent = boot == core::power::Boot::Timer;  // an app's deadline: the screen stays off unless it asks
  Serial.begin(SERIAL_BAUD);
  if (boot == core::power::Boot::Cold) {  // a wake must stay fast; only wait for a monitor on cold boot
    for (uint32_t t0 = millis(); !Serial && millis() - t0 < SERIAL_WAIT_MS;) delay(10);
  }
  ESP_LOGI(TAG, "%s %s, %s", APP_NAME, APP_VERSION,
           boot == core::power::Boot::Button ? "woke by button"
           : silent                          ? "woke by timer"
                                             : "cold boot");

  core::events::begin();
  core::store::begin();
  core::settings::begin();  // user brightness and Wi-Fi, before display and net use them
  core::ota::begin();
  if (!core::board::begin()) ESP_LOGE(TAG, "board bring-up failed");
  if (!core::display::begin(core::board::lcdColumnOffset(), !silent)) {
    ESP_LOGE(TAG, "display init failed; restarting");
    delay(2000);
    ESP.restart();
  }
  core::pmu::begin();
  core::clock::begin(boot != core::power::Boot::Cold);
  core::clock::resume();  // core::mono() carries on from before the sleep
  core::imu::begin(boot == core::power::Boot::Cold);
  core::net::begin();
  core::sound::begin();

  ui::begin();
  core::touch::begin(core::board::touchAddress(), core::power::onPress, core::power::activity);
  core::touch::setCalibration(core::settings::touchCal());
  core::buttons::begin(core::power::onPowerPress, core::power::onPress);
  if (boot == core::power::Boot::Button) core::buttons::ignoreHeldPress();

  apps::manager::setMenu(&menuApp);
  apps::manager::add(&haApp);
  apps::manager::add(&botApp);
  apps::manager::add(&pomodoroApp);
  apps::manager::add(&espressoApp);
  apps::manager::add(&diceApp);
  apps::manager::add(&levelApp);
  apps::manager::add(&timerApp);
  apps::manager::add(&noiseApp);
  apps::manager::add(&systemApp);
  const apps::Wake wake = wakeFor(boot);
  apps::manager::wake(wake);
  apps::manager::begin(apps::manager::resumeApp(wake));
  if (!silent) {
    ui::power_anim::on();
    // Draw the first frame before the brightness comes up, so the panel never
    // shows whatever its RAM held.
    lv_refr_now(core::display::lv());
  }

  core::ota::setProgressHook([]() {
    apps::manager::tick(millis());
    lv_timer_handler();
  });

  core::power::begin(
      {
          apps::manager::effectivePolicy,
          apps::manager::idleDimMs,
          apps::manager::enterMinimal,
          apps::manager::exitMinimal,
          apps::manager::minimalTick,
          apps::manager::minimalRefreshMs,
          apps::manager::minimalLevel,
          apps::manager::sleepNeed,
          apps::manager::prepareSleep,
          apps::manager::screenChanged,
      },
      boot);
}

void loop() {
  static uint32_t nextPmuMs = millis();  // first pass polls; never 0 (see core::reached)
  const uint32_t now = millis();

  core::pmu::KeyEvents keys{false, false};
  if (core::reached(now, nextPmuMs)) {
    nextPmuMs = now + PMU_POLL_MS;
    keys = core::pmu::poll();
  }
  core::buttons::poll(keys);

  core::Event e;
  while (core::events::poll(e)) dispatch(e);

  apps::manager::tick(now);
  core::power::update();
  core::net::loop();
  core::ota::loop();
  core::ota::healthTick();

  const uint32_t next = lv_timer_handler();
  // Dozing (screen off, sound playing): nothing to render, the buttons are
  // all there is to poll.
  const uint32_t most = core::power::dozing() ? LOOP_DELAY_DOZE_MS : LOOP_DELAY_MAX_MS;
  delay(next < most ? next : most);
}
