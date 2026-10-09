#pragma once
#include "app.h"

// Owns the registered apps, the menu and navigation. The menu screen is kept
// alive; other app screens are created on open and deleted on close.
namespace apps::manager {

void add(App *app);  // registration order = menu order
void setMenu(App *menu);
void begin();  // shows the menu
size_t count();
App *at(size_t i);

void open(App *app);
void back();
void home();
App *current();
bool inMenu();

void tick(uint32_t nowMs);  // service all apps, tick the visible one, pixel shift

// Power integration
power::Policy effectivePolicy();
uint32_t idleDimMs();
bool enterMinimal();  // false if no app wants a minimal view
void exitMinimal();
void minimalTick(uint32_t nowMs);
uint32_t minimalRefreshMs(uint32_t nowMs);
void prepareSleep();

}  // namespace apps::manager
