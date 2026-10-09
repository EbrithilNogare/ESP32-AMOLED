#include "app_manager.h"

#include <esp_log.h>
#include <pixel_shift.h>

#include "board.h"
#include "config.h"
#include "ui/theme.h"

namespace apps::manager {

static const char *TAG = "apps";
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

void add(App *app) {
  if (n < kMaxApps) list[n++] = app;
}
void setMenu(App *m) { menu = m; }
size_t count() { return n; }
App *at(size_t i) { return i < n ? list[i] : nullptr; }
App *current() { return cur; }
bool inMenu() { return cur == menu; }

static lv_obj_t *makeRoot(lv_obj_t *scr) {
  lv_obj_t *root = ui::box(scr);
  lv_obj_set_size(root, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(root, shift.x, shift.y);
  return root;
}

void begin() {
  menuScreen = ui::screen();
  menuRoot = makeRoot(menuScreen);
  menu->onCreate(menuRoot);
  cur = menu;
  lv_screen_load(menuScreen);
  nextShiftMs = PIXEL_SHIFT_PERIOD_MS;
}

void open(App *app) {
  if (!app || app == cur) return;
  if (cur != menu) return;  // apps are opened from the menu
  lv_obj_t *scr = ui::screen();
  appRoot = makeRoot(scr);
  cur = app;
  app->onCreate(appRoot);
  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, ui::motion::normal, 0, false);
  ESP_LOGI(TAG, "open %s", app->info().id);
}

static void closeToMenu() {
  if (cur == menu) return;
  cur->onDestroy();
  cur = menu;
  appRoot = nullptr;
  // auto_del: the app screen is deleted once the fade finishes; the app has
  // already dropped its widget pointers in onDestroy().
  lv_screen_load_anim(menuScreen, LV_SCREEN_LOAD_ANIM_FADE_IN, ui::motion::normal, 0, true);
}

void back() {
  if (cur->blocksNavigation()) return;
  if (cur->onBack()) return;
  closeToMenu();
}

void home() {
  if (cur->blocksNavigation()) return;
  if (cur == menu)
    menu->onBack();  // the menu treats this as "scroll to top"
  else
    closeToMenu();
}

static void applyShift() {
  lv_obj_set_pos(menuRoot, shift.x, shift.y);
  if (appRoot) lv_obj_set_pos(appRoot, shift.x, shift.y);
}

void tick(uint32_t nowMs) {
  for (size_t i = 0; i < n; i++) list[i]->service(nowMs);
  if (minimalApp) return;  // the minimal view is ticked by the power manager
  cur->onTick(nowMs);
  if (nowMs >= nextShiftMs) {
    nextShiftMs = nowMs + PIXEL_SHIFT_PERIOD_MS;
    shift = burnin::pixelShift(++shiftStep, PIXEL_SHIFT_MAX_PX);
    applyShift();
  }
}

power::Policy effectivePolicy() {
  power::Policy p = cur->powerPolicy();
  if (p == power::Policy::KeepAwake) return p;
  for (size_t i = 0; i < n; i++) {
    if (list[i] != cur && list[i]->backgroundPolicy() == power::Policy::LowPowerDisplay)
      return power::Policy::LowPowerDisplay;
  }
  return p;
}

uint32_t idleDimMs() { return cur->idleDimMs(); }

static App *minimalCandidate() {
  if (cur->powerPolicy() == power::Policy::LowPowerDisplay) return cur;
  for (size_t i = 0; i < n; i++)
    if (list[i]->backgroundPolicy() == power::Policy::LowPowerDisplay) return list[i];
  return nullptr;
}

bool enterMinimal() {
  if (minimalApp) return true;
  minimalApp = minimalCandidate();
  if (!minimalApp) return false;
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
}

void minimalTick(uint32_t nowMs) {
  if (minimalApp) minimalApp->minimalTick(nowMs);
}

uint32_t minimalRefreshMs(uint32_t nowMs) { return minimalApp ? minimalApp->minimalRefreshMs(nowMs) : 1000; }

void prepareSleep() {
  for (size_t i = 0; i < n; i++) list[i]->onSleep();
}

}  // namespace apps::manager
