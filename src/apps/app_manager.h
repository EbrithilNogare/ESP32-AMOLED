#pragma once
#include "app.h"

// Owns the registered apps, the menu and navigation. The menu screen is kept
// alive; other app screens are created on open and deleted on close.
namespace apps::manager {

void add(App *app);  // registration order = menu order
void setMenu(App *menu);
void wake(Wake w);        // every app's onWake(), once, before begin()
void begin(App *resume);  // shows the menu, or straight away `resume` (a button wake)
size_t count();
App *at(size_t i);
App *find(const char *id);  // registered app by AppInfo::id, nullptr if none
App *resumeApp(Wake w);     // the app to reopen after a wake (the one on screen at the sleep)

void open(App *app);
void back();
void home();
App *current();
bool inMenu();
// An app needs the user (alarm, phase end): the screen comes on and the app
// comes to the front, unless the one open blocks navigation (OTA).
void attention(App *app);

void tick(uint32_t nowMs);  // service all apps, tick the visible one, pixel shift

// Power integration
power::Policy effectivePolicy();
uint32_t idleDimMs();
bool enterMinimal();  // false if no app wants a minimal view
void exitMinimal();
void minimalTick(uint32_t nowMs);
uint32_t minimalRefreshMs(uint32_t nowMs);
uint8_t minimalLevel();
sleepplan::Need sleepNeed(uint64_t monoNow);  // every app's, merged
void prepareSleep(sleepplan::Kind kind);      // apps park their state; remembers the app on screen
void screenChanged(bool on);

}  // namespace apps::manager
