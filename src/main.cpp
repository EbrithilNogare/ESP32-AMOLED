// Wiring only (AGENTS.md): bring-up order in setup(), servicing in loop().
// The design is docs/ARCHITECTURE.md.
#include <Arduino.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <lvgl.h>

#include "apps/app_manager.h"
#include "apps/bot_face/bot_face_app.h"
#include "apps/dice/dice_app.h"
#include "apps/espresso/espresso_app.h"
#include "apps/home_assistant/ha_app.h"
#include "apps/menu/menu_app.h"
#include "apps/pomodoro/pomodoro_app.h"
#include "apps/system/system_app.h"
#include "config.h"
#include "core/board_init.h"
#include "core/buttons.h"
#include "core/display.h"
#include "core/events.h"
#include "core/net.h"
#include "core/ota.h"
#include "core/pmu.h"
#include "core/power.h"
#include "core/rtc_clock.h"
#include "core/sound.h"
#include "core/store.h"
#include "core/touch.h"
#include "ui/power_anim.h"
#include "ui/theme.h"

static const char *TAG = "main";

static apps::MenuApp menuApp;
static apps::HomeAssistantApp haApp;
static apps::BotFaceApp botApp([]() { return core::power::stage() == power::Stage::Dimmed; });
static apps::PomodoroApp pomodoroApp;
static apps::EspressoApp espressoApp;
static apps::DiceApp diceApp;
static apps::SystemApp systemApp;

static void dispatch(const core::Event &e) {
  using core::Ev;
  switch (e.type) {
    case Ev::Back: apps::manager::back(); break;
    case Ev::Home: apps::manager::home(); break;
    case Ev::LongPress:
      if (!apps::manager::current()->blocksNavigation()) core::power::requestSleep();  // never mid-OTA
      break;
    case Ev::HaResult: haApp.onResult(int(e.a), apps::ha_client::stateOf(e.b), apps::ha_client::resultOf(e.b)); break;
    case Ev::WifiChanged: haApp.onWifiChanged(); break;
    case Ev::TimeSynced: core::clock::onNtpSynced(); break;
    case Ev::VbusChanged: break;  // the power manager reads the PMU itself
  }
}

void setup() {
  const bool wokeByButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  Serial.begin(SERIAL_BAUD);
  if (!wokeByButton) {  // a wake must stay fast; only wait for a monitor on cold boot
    for (uint32_t t0 = millis(); !Serial && millis() - t0 < SERIAL_WAIT_MS;) delay(10);
  }
  ESP_LOGI(TAG, "%s %s, %s", APP_NAME, APP_VERSION, wokeByButton ? "woke by button" : "cold boot");

  core::events::begin();
  core::store::begin();
  core::ota::begin();
  if (!core::board::begin()) ESP_LOGE(TAG, "board bring-up failed");
  if (!core::display::begin(core::board::lcdColumnOffset())) {
    ESP_LOGE(TAG, "display init failed; restarting");
    delay(2000);
    ESP.restart();
  }
  core::pmu::begin();
  core::clock::begin();
  core::net::begin();
  core::sound::begin();

  ui::begin();
  core::touch::begin(core::board::touchAddress(), core::power::onPress, core::power::activity);
  core::buttons::begin(core::power::onPress);
  if (wokeByButton) core::buttons::ignoreHeldPress();

  apps::manager::setMenu(&menuApp);
  apps::manager::add(&haApp);
  apps::manager::add(&botApp);
  apps::manager::add(&pomodoroApp);
  apps::manager::add(&espressoApp);
  apps::manager::add(&diceApp);
  apps::manager::add(&systemApp);
  apps::manager::begin();
  ui::power_anim::on();
  // Draw the first frame before the brightness comes up, so the panel never
  // shows whatever its RAM held.
  lv_refr_now(core::display::lv());

  core::ota::setProgressHook([]() {
    apps::manager::tick(millis());
    lv_timer_handler();
  });

  core::power::begin({
      apps::manager::effectivePolicy,
      apps::manager::idleDimMs,
      apps::manager::enterMinimal,
      apps::manager::exitMinimal,
      apps::manager::minimalTick,
      apps::manager::minimalRefreshMs,
      apps::manager::prepareSleep,
  });
}

void loop() {
  static uint32_t nextPmuMs;
  const uint32_t now = millis();

  core::pmu::KeyEvents keys{false, false};
  if (now >= nextPmuMs) {
    nextPmuMs = now + 50;
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
  delay(next < 5 ? next : 5);
}
