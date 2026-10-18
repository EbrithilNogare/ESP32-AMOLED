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
#include <chrono>

#include <initializer_list>
#include <string>

#include "Arduino.h"
#include "apps/app_manager.h"
#include "apps/bot_face/bot_face_app.h"
#include "apps/dice/dice_app.h"
#include "apps/espresso/espresso_app.h"
#include "apps/g2048/g2048_app.h"
#include "apps/hephy/hephy_app.h"
#include "apps/home_assistant/ha_app.h"
#include "apps/level/level_app.h"
#include "apps/magic8/magic8_app.h"
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
#include "core/ticks.h"
#include "sim.h"
#include "ui/power_anim.h"
#include "ui/theme.h"

static apps::WifiApp wifiApp;  // opened from the quick settings, not listed in the menu
static apps::MenuApp menuApp(&wifiApp);
static apps::HomeAssistantApp haApp;
static apps::BotFaceApp botApp([]() { return sim::stage == power::Stage::Dimmed; });
static apps::HephyApp hephyApp([]() { return sim::stage == power::Stage::Dimmed; });
static apps::PomodoroApp pomodoroApp;
static apps::EspressoApp espressoApp;
static apps::DiceApp diceApp;
static apps::Magic8App magic8App;
static apps::G2048App g2048App;
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

// Like core::display: a band of the turned (landscape) screen is turned back
// into panel orientation, so the frames show what the panel shows.
static void flushCb(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
  static uint16_t turned[LCD_WIDTH * LCD_HEIGHT];
  lv_area_t phys = *area;
  const lv_area_t *a = &phys;
  const lv_display_rotation_t rot = lv_display_get_rotation(d);
  if (rot != LV_DISPLAY_ROTATION_0) {
    const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_display_rotate_area(d, &phys);
    lv_draw_rotate(px, turned, w, h, w * 2, lv_area_get_width(&phys) * 2, rot, LV_COLOR_FORMAT_RGB565);
    px = reinterpret_cast<uint8_t *>(turned);
  }
  const int32_t w = lv_area_get_width(a);
  const uint16_t *src = reinterpret_cast<const uint16_t *>(px);
  for (int32_t y = a->y1; y <= a->y2; y++) memcpy(&fb[y * LCD_WIDTH + a->x1], src + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

// Like core::touch: the panel's (emulated) raw position, corrected unless in raw mode.
static void readCb(lv_indev_t *, lv_indev_data_t *data) {
  const sim::PanelError &e = sim::panel;
  const int16_t rawX = int16_t((touchX - e.xOffset) / e.xScale + 0.5),
                rawY = int16_t((touchY - e.yOffset) / e.yScale + 0.5);
  data->point.x = sim::touchRaw ? rawX : touchcal::mapX(sim::touchCal, rawX);
  data->point.y = sim::touchRaw ? rawY : touchcal::mapY(sim::touchCal, rawY);
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

// A tap at a point of the screen as laid out (448 × 368 when turned):
// where the finger lands on the panel. Inverse of LVGL's pointer rotation.
static void tapScreen(int32_t x, int32_t y) {
  if (lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_270)
    tap(LCD_WIDTH - 1 - y, x);
  else
    tap(x, y);
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

// Every tappable element on screen must get a tap at its own centre: nothing
// on top of it, nothing between the drawing and the hit area. Elements that
// stay clickable only to pass drags on (no event callback) are skipped, and
// so are elements covered by something visible (the open quick-settings
// shade and its scrim over the menu): an invisible object taking the tap is
// the bug this looks for.
static int targetsChecked;
static bool isWithin(lv_obj_t *o, lv_obj_t *ancestor) {
  for (; o; o = lv_obj_get_parent(o))
    if (o == ancestor) return true;
  return false;
}
// Whether `hit`, or one of its ancestors that isn't also an ancestor of
// `target`, has a background you can see.
static bool coveredVisibly(lv_obj_t *hit, lv_obj_t *target) {
  for (lv_obj_t *o = hit; o && !isWithin(target, o); o = lv_obj_get_parent(o))
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > LV_OPA_TRANSP &&
        lv_obj_get_style_opa(o, LV_PART_MAIN) > LV_OPA_TRANSP)
      return true;
  return false;
}
static lv_obj_t *hitAt(const lv_point_t &p) {
  for (lv_obj_t *layer : {lv_layer_sys(), lv_layer_top(), lv_screen_active()})
    if (lv_obj_t *hit = lv_indev_search_obj(layer, const_cast<lv_point_t *>(&p))) return hit;
  return nullptr;
}
static void checkTargets(lv_obj_t *obj, const char *where) {
  if (lv_obj_is_hidden(obj)) return;
  if (lv_obj_is_clickable(obj) && lv_obj_get_event_count(obj) > 0 && lv_obj_is_visible(obj)) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_point_t c{(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
    if (c.x >= 0 && c.x < lv_display_get_horizontal_resolution(nullptr) && c.y >= 0 &&
        c.y < lv_display_get_vertical_resolution(nullptr)) {
      targetsChecked++;
      lv_obj_t *hit = hitAt(c);
      if (!hit || (!isWithin(hit, obj) && !coveredVisibly(hit, obj))) {
        printf("FAIL %s: tap at the centre (%d,%d) of a %dx%d target hits %s\n", where, int(c.x), int(c.y),
               int(lv_area_get_width(&a)), int(lv_area_get_height(&a)), hit ? "another object" : "nothing");
        failures++;
      }
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) checkTargets(lv_obj_get_child(obj, int32_t(i)), where);
}

static void capture(const char *name) {
  for (lv_obj_t *layer : {lv_screen_active(), lv_layer_top(), lv_layer_sys()}) checkTargets(layer, name);
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
  apps::manager::add(&magic8App);
  apps::manager::add(&levelApp);
  apps::manager::add(&timerApp);
  apps::manager::add(&noiseApp);
  apps::manager::add(&g2048App);
  apps::manager::add(&hephyApp);
  apps::manager::add(&systemApp);
  apps::manager::wake(apps::Wake::Cold);
  apps::manager::begin(nullptr);
  advance(100);

  // ---- menu
  CHECK(apps::manager::inMenu());
  capture("01-menu");

  // ---- lights: open, load the list from HA, receive states, toggle one
  auto lightInfo = [](const char *entity, const char *name, ha::LightState st) {
    ha::LightInfo l{};
    strlcpy(l.entity, entity, sizeof l.entity);
    strlcpy(l.name, name, sizeof l.name);
    l.state = st;
    return l;
  };
  tap(iconX(0), iconY(0));
  CHECK(apps::manager::current() == &haApp);
  advance(100);
  CHECK(sim::haListLoads == 1 && haApp.tileCount() == 0);  // the list first; nothing cached yet
  capture("02-lights-loading");
  // Sorted by name, as Home Assistant's list is parsed: TL, TR, BL, BR.
  sim::haLoaded = {lightInfo("light.bedroom", "Bedroom", ha::LightState::On),
                   lightInfo("light.hallway", "Hallway", ha::LightState::Off),
                   lightInfo("light.kitchen", "Kitchen", ha::LightState::Off),
                   lightInfo("light.porch", "Porch", ha::LightState::Unavailable)};
  haApp.onLights(ha::Result::Ok);
  CHECK(haApp.tileCount() == 4);
  const uint8_t gen = apps::ha_client::generation();
  advance(3100);
  CHECK(sim::haRefreshes >= 1);              // then the states poll
  tap(4 + 176 + 8 + 88, 4 + 216 + 8 + 108);  // bottom-right: Porch (n/a), no request
  CHECK(sim::haToggles.empty());
  tap(4 + 88, 4 + 216 + 8 + 108);  // bottom-left: Kitchen
  CHECK(sim::haToggles.size() == 1 && sim::haToggles[0] == 2);
  capture("03-lights-toggling");
  haApp.onResult(2, ha::LightState::Off, ha::Result::Ok, false, gen);  // a poll queued before the tap
  CHECK(haApp.pending(2));
  haApp.onResult(2, ha::LightState::On, ha::Result::Ok, true, uint8_t(gen + 1));  // for an old list: ignored
  CHECK(haApp.pending(2));
  haApp.onResult(2, ha::LightState::On, ha::Result::Ok, true, gen);
  CHECK(!haApp.pending(2));
  advance(300);
  capture("04-lights");
  haApp.onResult(0, ha::LightState::On, ha::Result::Unauthorized, false, gen);
  haApp.onResult(1, ha::LightState::Off, ha::Result::NetworkError, false, gen);  // keeps the banner
  advance(50);
  capture("05-lights-unauthorized");
  haApp.onResult(0, ha::LightState::On, ha::Result::Ok, false, gen);

  // More than four lights: a scrolling grid. The list is cached for next time.
  static const char *const kNames[] = {"Bedroom", "Desk", "Garden", "Hallway", "Kitchen", "Living room", "Porch"};
  sim::haLoaded.clear();
  for (int i = 0; i < 7; i++) {
    char id[32];
    snprintf(id, sizeof id, "light.l%d", i);
    sim::haLoaded.push_back(lightInfo(id, kNames[i], i % 3 ? ha::LightState::Off : ha::LightState::On));
  }
  haApp.onLights(ha::Result::Ok);
  advance(100);
  CHECK(haApp.tileCount() == 7);
  capture("46-lights-many");
  haApp.onLights(ha::Result::HostNotFound);  // a later reload fails: tiles stay, banner explains
  advance(50);
  CHECK(haApp.tileCount() == 7);
  capture("47-lights-not-found");
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());
  tap(iconX(0), iconY(0));  // reopened: the cached list is there at once
  advance(100);
  CHECK(haApp.tileCount() == 7);
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
  tapIcon(&pomodoroApp);
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
  tapIcon(&espressoApp);
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

  // The heaviest throw, 2 × 10 d20 (advantage), timed frame by frame while it
  // spins. Host times are far below the board's; compare runs, not devices.
  tap(dieX(5), 37);  // d20
  for (int i = 0; i < 9; i++) tap(22 + 142 - 21, barY);
  tap(263, barY);  // Adv
  CHECK(diceApp.die() == 20 && diceApp.count() == 10 && diceApp.mode() == dice::Mode::Adv);
  tap(184, 220);
  {
    using clk = std::chrono::steady_clock;
    double worst = 0, sum = 0;
    int frames = 0;
    while (diceApp.rolling()) {
      const auto t = clk::now();
      advance(5);  // a tick; the app redraws at DICE_FPS
      const double ms = std::chrono::duration<double, std::milli>(clk::now() - t).count();
      if (ms > 0.2) {  // a frame was rendered in this step
        sum += ms;
        frames++;
        if (ms > worst) worst = ms;
      }
      if (frames == 20) capture("48-dice-2x10d20-rolling");
    }
    printf("bench: dice 2x10 d20, %d frames, %.2f ms average, %.2f ms worst (host)\n", frames, sum / frames, worst);
  }
  advance(400);
  capture("49-dice-2x10d20");
  tap(206, barY);  // back to Flat
  for (int i = 0; i < 9; i++) tap(22 + 21, barY);
  tap(dieX(5), 37);
  apps::manager::back();
  advance(400);
  CHECK(apps::manager::inMenu());

  // ---- magic 8-ball: idle, a shake (IMU jolts), the answer; a tap asks too
  tapIcon(&magic8App);
  CHECK(apps::manager::current() == &magic8App);
  sim::accel = {0, 0, 1};
  advance(300);
  CHECK(magic8App.phase() == apps::Magic8App::Phase::Idle);
  capture("52-magic8-idle");
  for (int j = 0; j < 3; j++) {  // three jolts at 2 g, 200 ms apart
    sim::accel = {0.3f, 0.2f, 2.0f};
    advance(60);
    sim::accel = {0, 0, 1};
    advance(140);
  }
  CHECK(magic8App.phase() == apps::Magic8App::Phase::Shaking);
  capture("53-magic8-shaking");
  advance(MAGIC8_SETTLE_MS + 100);  // still: the answer surfaces
  CHECK(magic8App.phase() == apps::Magic8App::Phase::Revealing);
  advance(MAGIC8_REVEAL_MS + 100);
  CHECK(magic8App.phase() == apps::Magic8App::Phase::Answer && magic8App.answer() >= 0);
  capture("54-magic8-answer");
  {
    const int first = magic8App.answer();
    tap(184, 206);  // the ball
    advance(600 + MAGIC8_REVEAL_MS);
    CHECK(magic8App.phase() == apps::Magic8App::Phase::Answer && magic8App.answer() != first);
  }
  // Every answer's lines fit across the paint swipe (its solid part is ~300 px).
  for (int i = 0; i < eightball::kAnswers; i++) {
    char buf[32];
    strlcpy(buf, eightball::answer(i).text, sizeof buf);
    for (char *p = buf, *line; (line = strsep(&p, "\n"));) {
      lv_point_t sz;
      lv_text_get_size(&sz, line, &font_marker_30, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      if (sz.x > 290) printf("FAIL: 8-ball answer line \"%s\" is %d px wide\n", line, int(sz.x));
      failures += sz.x > 290;
    }
  }
  apps::manager::prepareSleep(sleepplan::Kind::Sleep);  // the answer is on screen again after a sleep
  {
    apps::Magic8App woken;
    woken.onWake(apps::Wake::FromSleep);
    CHECK(woken.phase() == apps::Magic8App::Phase::Answer && woken.answer() == magic8App.answer());
  }
  apps::manager::home();
  advance(400);

  // ---- hephy: fresh, coal, a forge, a Strike round; a day and a half away:
  // asleep, woken by a tap, the forged trinket revealed; the shelf
  tapIcon(&hephyApp);
  CHECK(apps::manager::current() == &hephyApp);
  advance(600);
  CHECK(hephyApp.view() == apps::HephyApp::View::Main && !hephyApp.asleep());
  CHECK(hephyApp.state().energy == 80 && hephyApp.state().forging == pet::kNone);
  CHECK(strcmp(hephyApp.status(), "Ready to forge") == 0);
  capture("55-hephy-main");
  const int32_t btnY = 350 + 28;
  touchX = 184 - 104;  // Coal
  touchY = btnY;
  touchDown = true;
  advance(40);
  touchDown = false;
  advance(260);
  capture("56-hephy-coal");  // the coal in flight
  advance(1600);
  CHECK(hephyApp.state().energy == 100);
  tap(184 - 104, btnY);  // full: he shakes his head
  CHECK(strcmp(hephyApp.status(), "I'm full!") == 0 && hephyApp.state().energy == 100);
  advance(2600);
  tap(184 + 104, btnY);  // Forge
  CHECK(hephyApp.state().forging != pet::kNone && hephyApp.state().energy == 75);
  CHECK(strstr(hephyApp.status(), "on the anvil") != nullptr);
  advance(700);
  capture("57-hephy-forging");
  const int64_t forgeLeft = pet::forgeLeftSec(hephyApp.state(), sim::wallSec);
  tap(184, btnY);  // Strike
  CHECK(hephyApp.view() == apps::HephyApp::View::Strike);
  const uint32_t xpBefore = hephyApp.state().xp;
  for (int i = 0; i < pet::kStrikes; i++) {
    tap(184, 120);  // anywhere strikes
    if (i == 1) capture("58-hephy-strike");
  }
  advance(1600);  // the round's result, then back
  CHECK(hephyApp.view() == apps::HephyApp::View::Main);
  CHECK(hephyApp.state().energy == 70 && hephyApp.state().xp >= xpBefore);
  CHECK(pet::forgeLeftSec(hephyApp.state(), sim::wallSec) <= forgeLeft);
  apps::manager::home();
  advance(400);
  sim::wallSec += 36 * 3600;  // a day and a half away: the forge is done, he fell asleep
  tapIcon(&hephyApp);
  advance(1500);
  CHECK(hephyApp.asleep() && !hephyApp.revealing());
  CHECK(hephyApp.state().gift != pet::kNone);
  capture("59-hephy-asleep");
  tap(184, 168);  // the face: he wakes up
  CHECK(!hephyApp.asleep());
  advance(1200);
  CHECK(hephyApp.revealing());
  capture("60-hephy-reveal");
  tap(184, 224);  // keep it
  advance(100);
  CHECK(!hephyApp.revealing() && pet::owned(hephyApp.state()) == 1 && hephyApp.state().gift == pet::kNone);
  tap(LCD_WIDTH - 24 - 28, 14 + 28);  // the shelf
  CHECK(hephyApp.view() == apps::HephyApp::View::Shelf);
  capture("61-hephy-shelf");
  CHECK(hephyApp.onBack() && hephyApp.view() == apps::HephyApp::View::Main);
  advance(100);
  apps::manager::home();
  advance(400);
  {  // everything is in NVS
    apps::HephyApp later(nullptr);
    later.onWake(apps::Wake::Cold);
    CHECK(pet::owned(later.state()) == 1 && later.state().xp == hephyApp.state().xp);
  }

  // ---- 2048: swipes slide and merge; 2048 reached; game over; new game
  tapIcon(&g2048App);
  CHECK(apps::manager::current() == &g2048App);
  advance(300);
  CHECK(g2048::tiles(g2048App.board()) == 2 && !g2048App.overlayShown());
  auto boardOf = [](std::initializer_list<int> exps, uint32_t score) {
    g2048::Board b{};
    int i = 0;
    for (int e : exps) b.cell[i++] = uint8_t(e);
    b.score = score;
    return b;
  };
  g2048App.setBoard(boardOf({1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 2, 0, 1}, 9620));
  advance(100);
  swipe(330, 60, 260);  // left on the board
  advance(300);
  CHECK(g2048App.board().cell[0] == 2 && g2048App.board().cell[1] == 2 && g2048App.board().score == 9624);
  CHECK(g2048::tiles(g2048App.board()) == 13);  // 12 after the merge, plus the new tile
  drag(184, 380, 120);                          // up
  advance(300);
  capture("62-2048-playing");
  g2048App.setBoard(boardOf({10, 10, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 20000));
  swipe(330, 60, 140);
  advance(400);
  CHECK(g2048App.board().won && g2048App.overlayShown());
  capture("63-2048-won");
  tapText("Keep going");
  advance(100);
  CHECK(!g2048App.overlayShown());
  // Stuck: reopening shows Game over; New game starts afresh, the best stays.
  g2048App.setBoard(boardOf({1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1}, 3120));
  apps::manager::home();
  advance(400);
  tapIcon(&g2048App);
  advance(300);
  CHECK(g2048App.overlayShown());
  capture("64-2048-over");
  CHECK(g2048App.best() >= 20004);
  tapText("New game");
  advance(100);
  CHECK(!g2048App.overlayShown() && g2048::tiles(g2048App.board()) == 2 && g2048App.board().score == 0);
  swipe(330, 60, 260);
  swipe(60, 330, 260);
  swipe(330, 60, 260);
  apps::manager::prepareSleep(sleepplan::Kind::Sleep);  // the same board after a sleep
  {
    apps::G2048App woken;
    woken.onWake(apps::Wake::FromSleep);
    CHECK(memcmp(&woken.board(), &g2048App.board(), sizeof(g2048::Board)) == 0 && woken.best() == g2048App.best());
  }
  apps::manager::home();
  advance(400);

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

  // Standing on the left edge turns the whole screen; the right edge doesn't.
  sim::accel = level::fromEdge(90, 0);
  advance(1200);
  CHECK(lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_0);
  sim::accel = level::fromEdge(-88.5f, 2);
  advance(1200);
  CHECK(levelApp.landscape());
  CHECK(lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_270);
  CHECK(lv_display_get_horizontal_resolution(nullptr) == LCD_HEIGHT);
  capture("50-level-landscape");
  tapScreen(LCD_HEIGHT - 16 - 58, LCD_WIDTH - 20 - 46 - 112 + 24);  // Zero, top of the right column
  CHECK(levelApp.zeroed());
  tapScreen(LCD_HEIGHT - 16 - 58, LCD_WIDTH - 20 - 46 - 112 + 24);  // Reset
  CHECK(!levelApp.zeroed());
  sim::accel = level::fromEdge(-30, 0);  // turned back toward the bottom edge
  advance(1200);
  CHECK(lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_0);
  sim::accel = level::fromEdge(-90, 0);
  advance(1200);
  CHECK(lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_270);
  apps::manager::back();  // leaving turns the screen back for the menu
  advance(400);
  CHECK(apps::manager::inMenu());
  CHECK(lv_display_get_rotation(nullptr) == LV_DISPLAY_ROTATION_0);
  sim::accel = level::fromFlat(0, 0);

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
  const int wakesBefore = sim::wakes;
  advance(51000);  // time's up while in the menu: the alarm beeps, the Timer comes to the front
  CHECK(timerApp.countdown().state() == timer::State::Done && timerApp.alarming());
  CHECK(!sim::beeps.empty() && sim::beeps.back() == int(core::sound::Pattern::Alarm));
  CHECK(apps::manager::current() == &timerApp && sim::wakes > wakesBefore);
  CHECK(timerApp.tab() == apps::TimerApp::Tab::Countdown);
  apps::manager::back();  // the first Back silences it; the overtime stays
  CHECK(!timerApp.alarming() && apps::manager::current() == &timerApp);
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
  CHECK(noiseApp.sleepNeed(0).awake);  // the screen may go off, the sound goes on (the chip dozes)
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
  tap(24 + 77, LCD_HEIGHT - 22 - 28);  // "Update"
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

  // ---- touch calibration, on a panel that reads 8 % wide and 15 px off on x, 6 % short on y
  sim::panel = {0.92, 15, 1.06, -12};
  tap(LCD_WIDTH - 24 - 77, LCD_HEIGHT - 22 - 28);  // "Touch" (still hit despite the error)
  CHECK(sim::touchRaw);
  capture("44-touch-calibration");
  static const int16_t kCx[] = {40, LCD_WIDTH - 40, LCD_WIDTH - 40, 40, LCD_WIDTH / 2};
  static const int16_t kCy[] = {40, 40, LCD_HEIGHT - 40, LCD_HEIGHT - 40, LCD_HEIGHT / 2};
  tap(kCx[0], kCy[0]);
  tap(kCx[1] - 60, kCy[1] + 50);  // a sloppy tap: the fit is refused, back to the first cross
  for (int i = 2; i < 5; i++) tap(kCx[i], kCy[i]);
  CHECK(sim::touchRaw);
  for (int i = 0; i < 5; i++) tap(kCx[i], kCy[i]);  // fingers on the crosses
  CHECK(!sim::touchRaw);                            // fitted and applied for the check
  touchX = 300;
  touchY = 120;
  touchDown = true;
  advance(50);
  capture("45-touch-check");  // the dot under the finger
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_next(nullptr), &p);
  CHECK(LV_ABS(p.x - 300) <= 2 && LV_ABS(p.y - 120) <= 2);
  touchDown = false;
  advance(100);
  tap(LCD_WIDTH - 24 - 77, LCD_HEIGHT - 22 - 28);  // Save
  CHECK(core::settings::touchCal().xOffset == sim::touchCal.xOffset && sim::touchCal.xScale != 65536);
  CHECK(LV_ABS(core::settings::touchCal().xOffset - 15) <= 1 && LV_ABS(core::settings::touchCal().yOffset + 12) <= 1);
  sim::panel = {};  // back to a perfect panel for the rest
  // Reset to factory: from the crosses, without tapping any.
  tap(LCD_WIDTH - 24 - 77, LCD_HEIGHT - 22 - 28);  // "Touch" again
  CHECK(sim::touchRaw);
  capture("49-touch-factory");
  tapText("Reset to factory");
  advance(50);
  CHECK(!sim::touchRaw && core::settings::touchCal().xScale == 65536 && core::settings::touchCal().xOffset == 0);
  CHECK(sim::touchCal.yScale == 65536 && sim::touchCal.yOffset == 0);  // applied too
  CHECK(findLabel(lv_screen_active(), "Firmware"));                    // back on the info view

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
  advance(40);  // held at the end long enough for a touch read there
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

  // The Wi-Fi row says what the radio does, not just a name.
  sim::radio = sim::wifi = false;
  qs.refresh();
  advance(50);
  CHECK(findLabel(lv_screen_active(), "Off"));
  capture("48-quick-settings-wifi-off");
  sim::radio = true;
  qs.refresh();
  CHECK(findLabel(lv_screen_active(), "Connecting…"));
  sim::wifi = true;
  qs.refresh();
  CHECK(findLabel(lv_screen_active(), "On · HomeNet"));

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

  // ---- saved networks: newest first, at most kWifiSaved, each with what was learned
  auto saveNet = [](const char *ssid, const char *password, uint8_t channel) {
    core::settings::WifiNet n{};
    strlcpy(n.ssid, ssid, sizeof n.ssid);
    strlcpy(n.password, password, sizeof n.password);
    n.channel = channel;
    core::settings::saveWifi(n);
  };
  saveNet("Old", "x", 1);
  for (int i = 0; i < core::settings::kWifiSaved; i++) {
    char name[8];
    snprintf(name, sizeof name, "Net%d", i);
    saveNet(name, "pw", uint8_t(i + 1));
  }
  CHECK(core::settings::wifiCount() == core::settings::kWifiSaved);
  CHECK(strcmp(core::settings::wifi(0).ssid, "Net4") == 0 && strcmp(core::settings::wifi(4).ssid, "Net0") == 0);
  saveNet("Net2", "new", 11);  // joined again: to the front, updated, no duplicate
  CHECK(strcmp(core::settings::wifi(0).ssid, "Net2") == 0 && strcmp(core::settings::wifi(0).password, "new") == 0);
  CHECK(core::settings::wifi(0).channel == 11 && core::settings::wifi(1).channel == 5);
  CHECK(strcmp(core::settings::wifi(1).ssid, "Net4") == 0 && strcmp(core::settings::wifi(2).ssid, "Net3") == 0);
  CHECK(core::settings::wifiCount() == core::settings::kWifiSaved);
  CHECK(core::settings::wifi(9).ssid[0] == '\0');  // out of range: empty

  // A saved network joins without the keypad.
  saveNet("Neighbour 5G", "pass1234", 6);
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

  // ---- the screen off: what every app needs (ARCHITECTURE §8)
  // Nothing running: deep sleep until the button.
  core::sound::stopNoise();
  sleepplan::Need need = apps::manager::sleepNeed(core::mono(sim::nowMs));
  CHECK(!need.awake);
  // A countdown asks for a wake at its end, noise keeps the chip up, a
  // pomodoro wakes at its phase end; merged, the earliest wake wins.
  tapIcon(&timerApp);
  advance(400);
  tapText("Countdown");
  advance(100);
  tapText("Start");  // 0:54
  advance(1000);
  uint64_t m = core::mono(sim::nowMs);
  need = timerApp.sleepNeed(m);
  CHECK(!need.awake && need.wakeAtMs > m + 52000 && need.wakeAtMs <= m + 53000);
  need = apps::manager::sleepNeed(m);
  CHECK(need.wakeAtMs == timerApp.sleepNeed(m).wakeAtMs);
  tapText("Stopwatch");
  advance(100);
  if (!timerApp.stopwatch().zero()) tapText(ICON_RESTART_ALT);  // left paused earlier: from zero
  tapText("Start");
  advance(2000);
  CHECK(timerApp.stopwatch().running() && timerApp.sleepNeed(m).wakeAtMs == need.wakeAtMs);  // it adds no wake
  core::sound::startNoise(noise::Kind::Rain);
  CHECK(apps::manager::sleepNeed(core::mono(sim::nowMs)).awake);
  core::sound::stopNoise();

  // ---- a deep sleep and the wake after it, emulated: park, move the clock,
  // and a fresh Timer picks up where this one was.
  m = core::mono(sim::nowMs);
  const uint64_t swBefore = timerApp.stopwatch().elapsedMs(m);
  apps::manager::prepareSleep(sleepplan::Kind::Sleep);
  core::store::rtcCommit();
  CHECK(strcmp(core::store::rtc().lastApp, "timer") == 0);
  CHECK(apps::manager::resumeApp(apps::Wake::FromSleep) == &timerApp);   // the power button reopens it
  CHECK(apps::manager::resumeApp(apps::Wake::FromPowerOff) == nullptr);  // a power-off opens the menu
  sim::monoOffset += 600000;                                             // ten minutes asleep
  {
    apps::TimerApp woken;
    woken.onWake(apps::Wake::FromSleep);
    const uint64_t now = core::mono(sim::nowMs);
    CHECK(woken.stopwatch().running());
    CHECK(woken.stopwatch().elapsedMs(now) >= swBefore + 600000 &&
          woken.stopwatch().elapsedMs(now) < swBefore + 601000);
    // Its end passed while asleep: due now (service() then rings, as the
    // countdown above did in the menu).
    CHECK(woken.countdown().state() == timer::State::Running && woken.countdown().remainingMs(now) == 0);
    CHECK(woken.sleepNeed(now).wakeAtMs == now);
    apps::TimerApp cold;
    cold.onWake(apps::Wake::Cold);  // a cold boot: settings only
    CHECK(!cold.stopwatch().running() && cold.countdown().state() == timer::State::Idle);
  }
  // Power-off pauses: it comes back paused, with its time.
  apps::manager::prepareSleep(sleepplan::Kind::PowerOff);
  CHECK(!timerApp.stopwatch().running());
  {
    apps::TimerApp woken;
    woken.onWake(apps::Wake::FromPowerOff);
    CHECK(!woken.stopwatch().running() && woken.stopwatch().elapsedMs(0) >= swBefore + 600000);
  }
  apps::manager::home();
  advance(400);

  // Dice: the last roll is on screen again after a sleep.
  tapIcon(&diceApp);
  advance(400);
  tap(184, 220);  // roll
  advance(DICE_ROLL_MS + 1200);
  CHECK(!diceApp.rolling() && diceApp.showingRoll());
  apps::manager::prepareSleep(sleepplan::Kind::Sleep);
  {
    apps::DiceApp woken;
    woken.onWake(apps::Wake::FromSleep);
    lv_obj_t *scr = ui::screen();
    lv_obj_t *root = ui::box(scr);
    lv_obj_set_size(root, LCD_WIDTH, LCD_HEIGHT);
    woken.onCreate(root);
    CHECK(woken.showingRoll() && !woken.rolling());
    woken.onDestroy();
    lv_obj_delete(scr);
  }
  apps::manager::home();
  advance(400);

  // ---- power animations: screen off (the CRT collapse) and power off. Both
  // end on a fully black screen: no row of the old picture left (that was a
  // bug: the covers stopped short of the line, which then faded).
  auto allBlack = []() {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
      if (fb[i]) return false;
    return true;
  };
  static bool animDone;
  ui::power_anim::off([]() { animDone = true; });
  advance(450);
  capture("27-power-off");
  advance(700);
  CHECK(animDone && allBlack());
  ui::power_anim::on();
  advance(POWER_ON_ANIM_MS + 100);
  CHECK(!ui::power_anim::running() && !allBlack());
  animDone = false;
  ui::power_anim::shutdown([]() { animDone = true; });
  advance(SHUTDOWN_ANIM_MS * 40 / 100);
  capture("50-shutdown-ring");
  advance(SHUTDOWN_ANIM_MS * 30 / 100);
  capture("51-shutdown-unwind");
  advance(SHUTDOWN_ANIM_MS * 30 / 100 + 100);
  CHECK(animDone && allBlack());

  printf("%d tap targets checked\n", targetsChecked);
  printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
