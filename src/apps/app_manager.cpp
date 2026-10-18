#include "app_manager.h"

#include <Arduino.h>
#include <esp_log.h>
#include <pixel_shift.h>

#include <string.h>

#include "board.h"
#include "config.h"
#include "core/power.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/theme.h"

namespace apps::manager {

[[maybe_unused]] static const char *TAG = "apps";
static constexpr size_t kMaxApps = 16;

static App *list[kMaxApps];
static size_t n;
static App *menu;
static App *cur;
static lv_obj_t *menuScreen, *menuRoot;
static lv_obj_t *appRoot;
static lv_obj_t *minimalLayer;
static App *minimalApp;
static uint32_t shiftStep, nextShiftMs;
static burnin::Offset shift{0, 0};
static bool screenOn = true;

void add(App *app) {
  if (n < kMaxApps) list[n++] = app;
}
void setMenu(App *m) { menu = m; }
size_t count() { return n; }
App *at(size_t i) { return i < n ? list[i] : nullptr; }
App *current() { return cur; }
bool inMenu() { return cur == menu; }

static bool rotated() { return lv_display_get_rotation(lv_display_get_default()) != LV_DISPLAY_ROTATION_0; }

static void setRotation(bool landscape) {
  lv_display_t *d = lv_display_get_default();
  lv_display_set_rotation(d, landscape ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_0);
  if (appRoot) lv_obj_set_size(appRoot, lv_display_get_horizontal_resolution(d), lv_display_get_vertical_resolution(d));
  ESP_LOGI(TAG, "%s", landscape ? "landscape" : "portrait");
}

// Only an open app (never the menu or a minimal view) may turn the screen.
static void syncRotation() {
  const bool want = cur != menu && !minimalApp && cur->landscape();
  if (want == rotated()) return;
  setRotation(want);
  cur->onRotated();
}

static lv_obj_t *makeRoot(lv_obj_t *scr) {
  lv_obj_t *root = ui::box(scr);
  lv_obj_set_size(root, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(root, shift.x, shift.y);
  return root;
}

App *find(const char *id) {
  for (size_t i = 0; i < n; i++)
    if (strcmp(list[i]->info().id, id) == 0) return list[i];
  return nullptr;
}

void wake(Wake w) {
  menu->onWake(w);
  for (size_t i = 0; i < n; i++) list[i]->onWake(w);
}

// After a sleep the app that was on screen comes back; after a power-off, or
// from the menu, the menu does.
App *resumeApp(Wake w) {
  if (w != Wake::FromSleep || !core::store::rtcValid()) return nullptr;
  core::store::RtcState &r = core::store::rtc();
  r.lastApp[sizeof r.lastApp - 1] = 0;
  App *a = r.lastApp[0] ? find(r.lastApp) : nullptr;
  return a && a->resumable() ? a : nullptr;
}

static void showApp(App *app, bool animate) {
  lv_obj_t *scr = ui::screen();
  appRoot = makeRoot(scr);
  cur = app;
  app->onCreate(appRoot);
  if (animate)
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, ui::motion::normal, 0, false);
  else
    lv_screen_load(scr);
  ESP_LOGI(TAG, "open %s", app->info().id);
}

void begin(App *resume) {
  menuScreen = ui::screen();
  menuRoot = makeRoot(menuScreen);
  menu->onCreate(menuRoot);
  cur = menu;
  lv_screen_load(menuScreen);
  nextShiftMs = millis() + PIXEL_SHIFT_PERIOD_MS;
  if (resume && resume != menu) showApp(resume, false);
}

void open(App *app) {
  if (!app || app == cur) return;
  if (cur != menu) return;  // apps are opened from the menu
  showApp(app, true);
}

static void closeToMenu(bool animate) {
  if (cur == menu) return;
  cur->onDestroy();
  if (rotated()) {  // the landscape layout would show sideways while it fades out
    lv_obj_clean(appRoot);
    setRotation(false);
  }
  cur = menu;
  lv_obj_t *old = lv_obj_get_screen(appRoot);
  appRoot = nullptr;
  if (!animate) {
    lv_screen_load(menuScreen);
    lv_obj_delete(old);
    return;
  }
  // auto_del: the app screen is deleted once the fade finishes; the app has
  // already dropped its widget pointers in onDestroy().
  lv_screen_load_anim(menuScreen, LV_SCREEN_LOAD_ANIM_FADE_IN, ui::motion::normal, 0, true);
}

void attention(App *app) {
  core::power::wake();
  if (!app || app == cur || cur->blocksNavigation()) return;
  if (minimalApp) exitMinimal();  // core::power ends the stage on its next pass
  closeToMenu(false);
  showApp(app, false);
}

void back() {
  if (cur->blocksNavigation()) return;
  if (cur->onBack()) return;
  closeToMenu(true);
}

void home() {
  if (cur->blocksNavigation()) return;
  if (cur == menu)
    menu->onBack();  // the menu treats this as "scroll to top"
  else
    closeToMenu(true);
}

static void applyShift() {
  lv_obj_set_pos(menuRoot, shift.x, shift.y);
  if (appRoot) lv_obj_set_pos(appRoot, shift.x, shift.y);
}

void tick(uint32_t nowMs) {
  for (size_t i = 0; i < n; i++) list[i]->service(nowMs);
  // The minimal view is ticked by the power manager, and does its own burn-in
  // drift (e.g. PomodoroApp::renderMinimal moves its content every redraw),
  // so the root shift pauses meanwhile.
  if (minimalApp || !screenOn) return;
  cur->onTick(nowMs);
  syncRotation();
  if (core::reached(nowMs, nextShiftMs)) {
    nextShiftMs = nowMs + PIXEL_SHIFT_PERIOD_MS;
    shift = burnin::pixelShift(++shiftStep, PIXEL_SHIFT_MAX_PX);
    applyShift();
  }
}

// The app that would show the minimal view: the open app if it asks for one,
// else the first closed app that does in the background. effectivePolicy()
// and enterMinimal() both use this, so they never disagree.
static App *minimalCandidate() {
  if (cur->powerPolicy() == power::Policy::LowPowerDisplay) return cur;
  for (size_t i = 0; i < n; i++)
    if (list[i] != cur && list[i]->backgroundPolicy() == power::Policy::LowPowerDisplay) return list[i];
  return nullptr;
}

// KeepAwake (open app) beats a minimal view (open or background app), which
// beats Normal. What happens once the screen is off is sleepNeed()'s business.
power::Policy effectivePolicy() {
  const power::Policy p = cur->powerPolicy();
  if (p == power::Policy::KeepAwake) return p;
  if (minimalCandidate()) return power::Policy::LowPowerDisplay;
  return p;
}

uint32_t idleDimMs() { return cur->idleDimMs(); }

bool enterMinimal() {
  if (minimalApp) return true;
  minimalApp = minimalCandidate();
  if (!minimalApp) return false;
  syncRotation();  // portrait before the first minimal frame
  minimalLayer = ui::box(lv_layer_top());
  lv_obj_set_size(minimalLayer, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(minimalLayer, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(minimalLayer, LV_OPA_COVER, 0);
  minimalApp->enterMinimal(minimalLayer);
  return true;
}

void exitMinimal() {
  if (!minimalApp) return;
  minimalApp->exitMinimal();
  lv_obj_delete(minimalLayer);
  minimalLayer = nullptr;
  minimalApp = nullptr;
  syncRotation();
}

void minimalTick(uint32_t nowMs) {
  if (minimalApp) minimalApp->minimalTick(nowMs);
}

uint32_t minimalRefreshMs(uint32_t nowMs) {
  return minimalApp ? minimalApp->minimalRefreshMs(nowMs) : MINIMAL_REFRESH_DEFAULT_MS;
}

uint8_t minimalLevel() { return minimalApp ? minimalApp->minimalBrightness() : 0; }

sleepplan::Need sleepNeed(uint64_t monoNow) {
  sleepplan::Need need = menu->sleepNeed(monoNow);
  for (size_t i = 0; i < n; i++) need = sleepplan::merge(need, list[i]->sleepNeed(monoNow));
  return need;
}

void prepareSleep(sleepplan::Kind kind) {
  menu->onSleep(kind);
  for (size_t i = 0; i < n; i++) list[i]->onSleep(kind);
  core::store::RtcState &r = core::store::rtc();
  const bool keep = cur != menu && cur->resumable();
  strncpy(r.lastApp, keep ? cur->info().id : "", sizeof r.lastApp - 1);
  r.lastApp[sizeof r.lastApp - 1] = 0;
}

void screenChanged(bool on) {
  screenOn = on;
  cur->onScreen(on);
}

}  // namespace apps::manager
