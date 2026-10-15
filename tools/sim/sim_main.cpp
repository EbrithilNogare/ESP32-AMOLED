// UI simulator and flow test: runs the real apps, UI and LVGL on the PC with
// the hardware modules stubbed (sim_core.cpp). It taps through every app,
// checks the resulting state, and writes a frame of each screen to
// $SIM_OUT (default .pio/sim) as PPM; tools/sim/shots.py turns them into PNGs.
//
//   pio run -e sim -t exec        (or: pio run -e sim && .pio/build/sim/program)
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "Arduino.h"
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
#include "board.h"
#include "core/settings.h"
#include "core/sound.h"
#include "core/store.h"
#include "sim.h"
#include "ui/power_anim.h"
#include "ui/theme.h"

static apps::WifiApp wifiApp;  // opened from the quick settings, not listed in the menu
static apps::MenuApp menuApp(&wifiApp);
static apps::HomeAssistantApp haApp;
static apps::BotFaceApp botApp([]() { return sim::stage == power::Stage::Dimmed; });
static apps::PomodoroApp pomodoroApp;
static apps::EspressoApp espressoApp;
static apps::DiceApp diceApp;
static apps::LevelApp levelApp;
static apps::TimerApp timerApp;
static apps::NoiseApp noiseApp;
static apps::SystemApp systemApp;

static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];
static int32_t touchX, touchY;
static bool touchDown;
static std::string outDir = ".pio/sim";
static int failures;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const int32_t w = lv_area_get_width(a);
  const uint16_t *src = reinterpret_cast<const uint16_t *>(px);
  for (int32_t y = a->y1; y <= a->y2; y++) memcpy(&fb[y * LCD_WIDTH + a->x1], src + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

static void readCb(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = touchX;
  data->point.y = touchY;
  data->state = touchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void advance(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 5) {
    sim::nowMs += 5;
    apps::manager::tick(sim::nowMs);
    lv_timer_handler();
  }
}

static void tap(int32_t x, int32_t y) {
  touchX = x;
  touchY = y;
  touchDown = true;
  advance(80);
  touchDown = false;
  advance(400);  // let screen transitions finish
}

static void longPress(int32_t x, int32_t y) {
  touchX = x;
  touchY = y;
  touchDown = true;
  advance(900);
  touchDown = false;
  advance(300);
}

static void drag(int32_t x, int32_t y0, int32_t y1) {
  touchX = x;
  touchY = y0;
  touchDown = true;
  advance(40);
  const int steps = 20;
  for (int i = 1; i <= steps; i++) {
    touchY = y0 + (y1 - y0) * i / steps;
    advance(15);
  }
  advance(300);  // hold still before lifting: no momentum, so it snaps to the nearest row
  touchDown = false;
  advance(800);
}

static void capture(const char *name) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  const std::string path = outDir + "/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    printf("cannot write %s\n", path.c_str());
    failures++;
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
  for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
    const uint16_t p = fb[i];
    const uint8_t rgb[3] = {uint8_t((p >> 11) << 3 | (p >> 13)), uint8_t(((p >> 5) & 0x3F) << 2 | ((p >> 9) & 3)),
                            uint8_t((p & 0x1F) << 3 | ((p >> 2) & 7))};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("frame %s\n", name);
}

// Menu icon centres: two 128 px discs per 176 px row, 40 px apart, list at y 36.
static int32_t iconX(int i) { return (LCD_WIDTH - 296) / 2 + 64 + (i % 2) * 168; }
static int32_t iconY(int i) { return 36 + 8 + 64 + (i / 2) * 176; }

// Finds the menu icon whose click handler opens `app` (its event user data).
static lv_obj_t *findIcon(lv_obj_t *obj, const apps::App *app) {
  for (uint32_t i = 0; i < lv_obj_get_event_count(obj); i++) {
    if (lv_event_dsc_get_user_data(lv_obj_get_event_dsc(obj, i)) == app) return obj;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    if (lv_obj_t *found = findIcon(lv_obj_get_child(obj, i), app)) return found;
  }
  return nullptr;
}

// Scrolls the menu (a row at a time) until the app's icon is on screen, then taps it.
static void tapIcon(const apps::App *app) {
  lv_obj_t *icon = findIcon(lv_screen_active(), app);
  if (!icon) {
    printf("FAIL: no menu icon for %s\n", app->info().id);
    failures++;
    return;
  }
  lv_area_t a;
  for (int i = 0; i < 6; i++) {
    lv_obj_get_coords(icon, &a);
    const int32_t cy = (a.y1 + a.y2) / 2;
    if (cy > LCD_HEIGHT - 60)
      drag(184, 380, 204);
    else if (cy < 60)
      drag(184, 204, 380);
    else
      break;
  }
  tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

// The first visible label showing exactly `text`, depth first.
static lv_obj_t *findLabel(lv_obj_t *obj, const char *text) {
  if (lv_obj_is_visible(obj) && lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0)
    return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    if (lv_obj_t *found = findLabel(lv_obj_get_child(obj, i), text)) return found;
  }
  return nullptr;
}

static void tapText(const char *text) {
  lv_obj_t *l = findLabel(lv_screen_active(), text);
  if (!l) {
    printf("FAIL: no label \"%s\" on screen\n", text);
    failures++;
    return;
  }
  lv_area_t a;
  lv_obj_get_coords(l, &a);
  tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

static void swipe(int32_t x0, int32_t x1, int32_t y) {
  touchX = x0;
  touchY = y;
  touchDown = true;
  advance(30);
  for (int i = 1; i <= 8; i++) {
    touchX = x0 + (x1 - x0) * i / 8;
    advance(15);
  }
  touchDown = false;
  advance(300);
}

int main() {
  if (const char *o = getenv("SIM_OUT")) outDir = o;
  mkdir(outDir.c_str(), 0755);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);  // plain RGB565 for the frame dump
  static uint8_t buf[LCD_WIDTH * 40 * 2];
  lv_display_set_buffers(disp, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);

  ui::begin();
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
  apps::manager::begin();
  advance(100);

  // ---- menu
  CHECK(apps::manager::inMenu());
  capture("01-menu");

  // ---- lights: open, receive states, toggle one
  tap(iconX(0), iconY(0));
  CHECK(apps::manager::current() == &haApp);
  capture("02-lights-loading");
  advance(100);
  CHECK(sim::haRefreshes >= 1);
  haApp.onResult(0, ha::LightState::On, ha::Result::Ok, false);
  haApp.onResult(1, ha::LightState::Off, ha::Result::Ok, false);
  haApp.onResult(2, ha::LightState::Off, ha::Result::Ok, false);
  haApp.onResult(3, ha::LightState::Unknown, ha::Result::NotFound, false);
  advance(50);
  tap(4 + 176 + 8 + 88, 4 + 216 + 8 + 108);  // bottom-left is index 2; this is bottom-right (n/a): no request
  CHECK(sim::haToggles.empty());
  tap(4 + 88, 4 + 216 + 8 + 108);  // bottom-left: Bedroom
  CHECK(sim::haToggles.size() == 1 && sim::haToggles[0] == 2);
  capture("03-lights-toggling");
  haApp.onResult(2, ha::LightState::Off, ha::Result::Ok, false);  // a poll queued before the tap
  CHECK(haApp.pending(2));
  haApp.onResult(2, ha::LightState::On, ha::Result::Ok, true);
  CHECK(!haApp.pending(2));
  advance(300);
  capture("04-lights");
  haApp.onResult(0, ha::LightState::On, ha::Result::Unauthorized, false);
  haApp.onResult(1, ha::LightState::Off, ha::Result::NetworkError, false);  // keeps the banner
  advance(50);
  capture("05-lights-unauthorized");
  haApp.onResult(0, ha::LightState::On, ha::Result::Ok, false);
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- bot face: idle, tap, double tap, dimmed
  tap(iconX(1), iconY(1));
  CHECK(apps::manager::current() == &botApp);
  advance(1000);
  capture("06-bot-face");
  touchX = 184;
  touchY = 224;
  touchDown = true;
  advance(40);
  touchDown = false;
  advance(250);
  capture("07-bot-face-happy");
  advance(1500);
  sim::stage = power::Stage::Dimmed;
  advance(1500);
  capture("08-bot-face-sleepy");
  sim::stage = power::Stage::Active;
  apps::manager::home();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- pomodoro: start, run, minimal view, back to menu while running
  tap(iconX(2), iconY(2));
  CHECK(apps::manager::current() == &pomodoroApp);
  capture("09-pomodoro-ready");
  tap(184, 216);  // the ring area toggles
  CHECK(pomodoroApp.powerPolicy() == power::Policy::LowPowerDisplay);
  advance(5000);
  capture("10-pomodoro-running");
  CHECK(apps::manager::enterMinimal());
  apps::manager::minimalTick(sim::nowMs);
  advance(50);
  capture("11-pomodoro-minimal");
  apps::manager::exitMinimal();
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());
  CHECK(apps::manager::effectivePolicy() == power::Policy::LowPowerDisplay);  // keeps running in the background

  // ---- espresso: scroll the menu one row (176 px), run a 27.4 s shot
  drag(184, 380, 204);
  capture("12-menu-scrolled");
  tap(iconX(3), iconY(3) - 176);  // after snapping one row up
  CHECK(apps::manager::current() == &espressoApp);
  capture("13-espresso-ready");
  tap(184, 200);
  CHECK(espressoApp.powerPolicy() == power::Policy::KeepAwake);
  advance(22300);
  capture("14-espresso-running");
  advance(5100 - 480);  // tap() itself takes 80 ms before release
  tap(184, 200);
  capture("15-espresso-done");
  CHECK(espressoApp.powerPolicy() == power::Policy::Normal);
  CHECK(espressoApp.timer().lastShotDs() == 274 && espressoApp.timer().historyCount() == 1);
  // Taps on the history strip count as "anywhere"; a long press discards.
  const int32_t stripY = LCD_HEIGHT - 20 - 20;
  tap(184, stripY);  // Done → Ready
  tap(184, stripY);  // Ready → Running
  CHECK(espressoApp.timer().mode() == shot::Mode::Running);
  advance(3000);
  tap(184, stripY);  // stop: a 3 s shot
  CHECK(espressoApp.timer().historyCount() == 2);
  longPress(184, 200);
  CHECK(espressoApp.timer().mode() == shot::Mode::Ready && espressoApp.timer().historyCount() == 1);
  apps::manager::back();  // Ready → menu
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- dice: idle, mid-spin, d20 with advantage, 6d6 with advantage, d12, d100
  tapIcon(&diceApp);
  CHECK(apps::manager::current() == &diceApp);
  CHECK(diceApp.die() == 20 && diceApp.count() == 1 && diceApp.mode() == dice::Mode::Flat);
  capture("16-dice-idle");
  auto dieX = [](int i) { return int32_t(24 + i * (LCD_WIDTH - 90) / 6.0f + 21); };
  const int32_t barY = LCD_HEIGHT - 16 - 24;
  tap(184, 220);  // the stage rolls
  CHECK(diceApp.rolling());
  capture("17-dice-rolling");
  advance(1600);
  CHECK(!diceApp.rolling());
  tap(263, barY);  // Adv
  CHECK(diceApp.mode() == dice::Mode::Adv);
  tap(184, 220);
  advance(1800);
  capture("18-dice-d20-adv");
  tap(dieX(1), 37);  // d6
  CHECK(diceApp.die() == 6);
  for (int i = 0; i < 5; i++) tap(22 + 142 - 21, barY);  // + five times
  CHECK(diceApp.count() == 6);
  tap(184, 220);
  advance(3000);
  CHECK(!diceApp.rolling());
  capture("19-dice-6d6-adv");
  tap(206, barY);  // Flat
  CHECK(diceApp.mode() == dice::Mode::Flat);
  for (int i = 0; i < 6; i++) tap(22 + 21, barY);  // − down to 1 (one extra tap is ignored)
  CHECK(diceApp.count() == 1);
  tap(dieX(4), 37);  // d12
  CHECK(diceApp.die() == 12);
  tap(184, 220);
  advance(1800);
  capture("20-dice-d12");
  tap(dieX(6), 37);  // d100
  CHECK(diceApp.die() == 100);
  tap(184, 220);
  advance(1800);
  capture("21-dice-d100");
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- level: flat and tilted, flat and level, on an edge; Hold and Zero
  tapIcon(&levelApp);
  CHECK(apps::manager::current() == &levelApp);
  sim::accel = level::fromFlat(2.4f, -1.1f);
  advance(800);
  CHECK(levelApp.view() == level::View::Flat);
  capture("22-level-flat");
  sim::accel = level::fromFlat(0.1f, -0.1f);
  advance(800);
  capture("23-level-flat-level");
  sim::accel = level::fromEdge(12, 4);
  advance(1200);
  CHECK(levelApp.view() == level::View::Edge);
  capture("24-level-edge");
  const int32_t levelBarY = LCD_HEIGHT - 16 - 24;
  tap(126 + 48, levelBarY);  // Hold
  CHECK(levelApp.held());
  sim::accel = level::fromFlat(0, 0);
  advance(600);
  CHECK(levelApp.view() == level::View::Edge);  // the held reading stays on screen
  tap(126 + 48, levelBarY);
  CHECK(!levelApp.held());
  advance(600);
  CHECK(levelApp.view() == level::View::Flat);
  tap(22 + 48, levelBarY);  // Zero
  CHECK(levelApp.zeroed());
  tap(22 + 48, levelBarY);  // Reset
  CHECK(!levelApp.zeroed());
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- timer: the clock (and its minimal view), stopwatch, countdown
  tapIcon(&timerApp);
  CHECK(apps::manager::current() == &timerApp && timerApp.tab() == apps::TimerApp::Tab::Clock);
  CHECK(timerApp.powerPolicy() == power::Policy::LowPowerDisplay);  // the clock never lets the screen go off
  capture("32-timer-clock");
  CHECK(apps::manager::enterMinimal());
  CHECK(apps::manager::minimalLevel() == TIMER_MINIMAL_BRIGHTNESS);
  apps::manager::minimalTick(sim::nowMs);
  advance(50);
  capture("33-timer-clock-minimal");
  apps::manager::exitMinimal();

  tapText("Stopwatch");
  CHECK(timerApp.tab() == apps::TimerApp::Tab::Stopwatch);
  CHECK(timerApp.powerPolicy() == power::Policy::Normal);  // nothing running: may sleep
  tapText("Start");
  CHECK(timerApp.stopwatch().running());
  CHECK(timerApp.powerPolicy() == power::Policy::LowPowerDisplay);
  advance(75400 - 400);
  capture("34-timer-stopwatch");
  tapText("Pause");
  CHECK(!timerApp.stopwatch().running());

  tapText("Countdown");
  CHECK(timerApp.tab() == apps::TimerApp::Tab::Countdown);
  CHECK(timerApp.countdown().seconds() == TIMER_DEFAULT_COUNTDOWN_S);
  // Wheels: minutes x 44-164, seconds x 204-324, y 92-328. Tap above the
  // middle to count up, below to count down, or drag (16 px a step).
  tap(104, 130);        // 6:00
  drag(264, 150, 250);  // 100 px down: 6 steps → 5:54... of the seconds: 6:54
  CHECK(timerApp.countdown().seconds() == 6 * 60 + 54);
  capture("35-timer-countdown-set");
  for (int i = 0; i < 6; i++) tap(104, 290);  // down to 0:54
  CHECK(timerApp.countdown().seconds() == 54);
  tapText("Start");
  CHECK(timerApp.countdown().running());
  advance(4000);
  capture("36-timer-countdown-running");
  apps::manager::back();  // keeps running in the background
  advance(400);
  CHECK(apps::manager::inMenu());
  CHECK(timerApp.backgroundPolicy() == power::Policy::LowPowerDisplay);
  advance(51000);  // time's up while in the menu: the alarm beeps
  CHECK(timerApp.countdown().state() == timer::State::Done && timerApp.alarming());
  CHECK(!sim::beeps.empty() && sim::beeps.back() == int(core::sound::Pattern::Alarm));
  tapIcon(&timerApp);  // opening it silences the alarm and shows the overtime
  CHECK(!timerApp.alarming() && timerApp.tab() == apps::TimerApp::Tab::Countdown);
  advance(2000);
  capture("37-timer-countdown-done");
  tapText("Stop");
  CHECK(timerApp.countdown().state() == timer::State::Idle && timerApp.countdown().seconds() == 54);
  tapText("Clock");
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- noise: rain, then swipe through white and blue noise
  tapIcon(&noiseApp);
  CHECK(apps::manager::current() == &noiseApp && !noiseApp.playing());
  CHECK(noiseApp.kind() == noise::Kind::Rain);
  advance(200);
  capture("38-noise-paused");
  tap(184, 224);
  CHECK(sim::noise == int(noise::Kind::Rain));
  CHECK(noiseApp.powerPolicy() == power::Policy::NoSleep);  // screen may go black, the sound goes on
  advance(1500);
  capture("39-noise-rain");
  advance(4000);
  CHECK(!noiseApp.chromeShown());  // labels fade out while it plays untouched
  capture("40-noise-rain-calm");
  swipe(300, 80, 260);  // left: next
  CHECK(noiseApp.kind() == noise::Kind::White && sim::noise == int(noise::Kind::White));
  CHECK(noiseApp.chromeShown());
  advance(1500);
  capture("41-noise-white");
  swipe(300, 80, 260);
  CHECK(noiseApp.kind() == noise::Kind::Blue && sim::noise == int(noise::Kind::Blue));
  advance(1500);
  capture("42-noise-blue");
  swipe(80, 300, 260);  // right: previous
  CHECK(noiseApp.kind() == noise::Kind::White);
  swipe(80, 300, 260);
  CHECK(noiseApp.kind() == noise::Kind::Rain);
  apps::manager::back();  // the rain keeps playing in the menu
  advance(400);
  CHECK(sim::noise == int(noise::Kind::Rain));
  // The running pomodoro's minimal view wins over NoSleep (it never sleeps
  // either, and skips light sleep while sound plays).
  CHECK(apps::manager::effectivePolicy() == power::Policy::LowPowerDisplay);
  tapIcon(&noiseApp);
  tap(184, 224);  // pause
  CHECK(sim::noise == -1 && noiseApp.powerPolicy() == power::Policy::Normal);
  apps::manager::back();
  advance(400);

  // ---- system + wireless update
  tapIcon(&systemApp);
  CHECK(apps::manager::current() == &systemApp);
  capture("25-system");
  tap(184, LCD_HEIGHT - 22 - 28);  // "Wireless update"
  CHECK(systemApp.powerPolicy() == power::Policy::KeepAwake);
  sim::otaState = core::ota::State::Receiving;
  sim::otaProgress = 42;
  advance(100);
  capture("26-wireless-update");
  CHECK(systemApp.blocksNavigation());
  apps::manager::home();  // ignored mid-transfer
  advance(100);
  CHECK(apps::manager::current() == &systemApp);
  sim::otaState = core::ota::State::Waiting;
  apps::manager::back();  // leaves the update view
  advance(100);
  CHECK(systemApp.powerPolicy() == power::Policy::Normal);
  apps::manager::home();
  advance(400);

  // ---- quick settings: pull the status bar down
  apps::manager::home();  // the menu scrolls back to the top
  advance(400);
  apps::QuickSettings &qs = menuApp.quickSettings();
  drag(184, 14, 230);
  CHECK(qs.isOpen());
  capture("28-quick-settings");
  apps::manager::back();  // Back closes the shade
  advance(400);
  CHECK(!qs.isOpen());
  tap(184, 14);  // a tap on the status bar opens it too
  CHECK(qs.isOpen());

  // Brightness bar (y 148-212, x 16-352): a tap in the middle, then a drag;
  // live while touched, saved on release. sx: the pixel shift of the menu
  // by now, which moves the bars too.
  const int32_t sx = lv_obj_get_x(lv_obj_get_child(lv_screen_active(), 0));
  const int applied = sim::brightnessApplied;
  tap(184 + sx, 180);
  CHECK(core::settings::brightnessPct() == 38 && sim::brightnessApplied > applied);
  touchX = 100 + sx;  // press at x 100 and slide to x 300
  touchY = 180;
  touchDown = true;
  advance(40);
  for (int i = 1; i <= 10; i++) {
    touchX = 100 + sx + 20 * i;
    advance(15);
  }
  touchDown = false;
  advance(100);
  CHECK(core::settings::brightnessPct() == 60);
  uint8_t saved = 0;
  CHECK(core::store::load("bright", &saved, 1) && saved == 60);

  // Volume (mute button x 16-80, bar x 92-352, y 224-288): the bar sets it
  // and blips on release; the button mutes, and muted means no beeps.
  const int volApplied = sim::volumeApplied;
  sim::beeps.clear();
  tap(92 + 130 + sx, 256);
  CHECK(core::settings::volumePct() == 50 && sim::volumeApplied > volApplied);
  CHECK(sim::beeps.size() == 1 && sim::beeps[0] == int(core::sound::Pattern::Confirm));
  tap(48, 256);
  CHECK(core::settings::muted());
  uint8_t vol[2] = {};
  CHECK(core::store::load("volume", vol, 2) && vol[0] == 50 && vol[1] == 1);
  capture("43-quick-settings-muted");
  core::sound::play(core::sound::Pattern::Confirm);
  CHECK(sim::beeps.size() == 1);
  tap(48, 256);
  CHECK(!core::settings::muted());

  // ---- Wi-Fi: open from the shade, pick a network, type the password
  sim::networks = {{"HomeNet", -52, true}, {"Neighbour 5G", -68, true}, {"Cafe Guest", -80, false}};
  sim::scanDone = false;
  tap(184, 104);  // the Wi-Fi row
  CHECK(apps::manager::current() == &wifiApp && !qs.isOpen());
  CHECK(sim::scans == 1);
  sim::scanDone = true;
  advance(100);
  capture("29-wifi-networks");
  tap(184, 60 + 56 + 6 + 28);  // "Neighbour 5G"
  CHECK(wifiApp.view() == apps::WifiApp::View::Password);

  // Keys: 3 × 4 from y 136, 106 × 54 px, 8 px apart (6 between rows).
  static const char kKeyOrder[] = "123456789*0#";
  auto key = [](char k, int times) {
    const int i = int(strchr(kKeyOrder, k) - kKeyOrder);
    for (int t = 0; t < times; t++) tap(16 + (i % 3) * 114 + 53, 136 + (i / 3) * 60 + 27);
  };
  auto pause = []() { advance(1000); };  // lets the pending letter commit
  key('7', 1);                           // p
  key('2', 1);                           // a
  key('7', 4);                           // s
  pause();
  key('7', 3);  // r, still cycling
  capture("30-wifi-password");
  key('7', 1);  // r → s
  key('#', 2);  // abc → ABC → 123 (commits the s)
  key('1', 1);
  key('2', 1);
  key('3', 1);
  key('4', 1);
  CHECK(strcmp(wifiApp.typed(), "pass1234") == 0);
  // Taps in the gaps and out at the bezel still reach the nearest key.
  tap(4, LCD_HEIGHT - 4);  // bottom-left corner: Backspace
  CHECK(strcmp(wifiApp.typed(), "pass123") == 0);
  tap(2, 136 + 60 + 27);  // left of "4", past the margin
  CHECK(strcmp(wifiApp.typed(), "pass1234") == 0);
  tap(16 + 106 + 3, 136 + 54 + 2);  // in the gaps, nearest "1"
  CHECK(strcmp(wifiApp.typed(), "pass12341") == 0);
  tap(16 + 53, LCD_HEIGHT - 20 - 24);  // Backspace
  CHECK(strcmp(wifiApp.typed(), "pass1234") == 0);
  tap(241, LCD_HEIGHT - 20 - 24);  // Connect
  CHECK(wifiApp.view() == apps::WifiApp::View::Joining);
  CHECK(sim::joinSsid == "Neighbour 5G" && sim::joinPassword == "pass1234");
  sim::join = core::net::Join::Joined;
  advance(200);
  capture("31-wifi-connected");
  tap(184, LCD_HEIGHT - 20 - 28);  // Done
  CHECK(apps::manager::inMenu());

  // ---- saved networks: newest first, at most kWifiSaved
  core::settings::saveWifi("Old", "x");
  for (int i = 0; i < core::settings::kWifiSaved; i++) {
    char name[8];
    snprintf(name, sizeof name, "Net%d", i);
    core::settings::saveWifi(name, "pw");
  }
  CHECK(core::settings::wifiCount() == core::settings::kWifiSaved);
  CHECK(strcmp(core::settings::wifiSsid(0), "Net4") == 0 && strcmp(core::settings::wifiSsid(4), "Net0") == 0);
  core::settings::saveWifi("Net2", "new");  // joined again: to the front, new password, no duplicate
  CHECK(strcmp(core::settings::wifiSsid(0), "Net2") == 0 && strcmp(core::settings::wifiPassword(0), "new") == 0);
  CHECK(strcmp(core::settings::wifiSsid(1), "Net4") == 0 && strcmp(core::settings::wifiSsid(2), "Net3") == 0);
  CHECK(core::settings::wifiCount() == core::settings::kWifiSaved);

  // A saved network joins without the keypad.
  core::settings::saveWifi("Neighbour 5G", "pass1234");
  sim::join = core::net::Join::Idle;
  apps::manager::open(&wifiApp);
  advance(400);
  CHECK(core::net::known("Neighbour 5G") && !core::net::known("Cafe Guest"));
  tap(184, 60 + 56 + 6 + 28);  // "Neighbour 5G"
  CHECK(wifiApp.view() == apps::WifiApp::View::Joining);
  CHECK(sim::joinSsid == "Neighbour 5G" && sim::joinPassword == "(saved)");
  sim::join = core::net::Join::Joined;
  advance(200);
  tap(184, LCD_HEIGHT - 20 - 28);  // Done
  CHECK(apps::manager::inMenu());

  // ---- power-off animation
  static bool animDone;
  ui::power_anim::off([]() { animDone = true; });
  advance(450);
  capture("27-power-off");
  advance(700);
  CHECK(animDone);

  printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
