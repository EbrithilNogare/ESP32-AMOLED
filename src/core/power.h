#pragma once
#include <power_fsm.h>
#include <stdint.h>

// PowerManager (ARCHITECTURE §3, §8): picks the battery/charger profile, runs
// the idle timeline through PowerFsm, fades brightness, switches the panel
// off when black, shows the minimal view with light sleep in between, and
// enters deep sleep (after the power-off animation for a long press).
namespace core::power {

struct Hooks {
  ::power::Policy (*policy)();
  uint32_t (*idleDimMs)();  // app override of the dim delay, 0 = default
  bool (*enterMinimal)();   // false: no app has a minimal view (Black is shown instead)
  void (*exitMinimal)();
  void (*minimalTick)(uint32_t nowMs);
  uint32_t (*minimalRefreshMs)(uint32_t nowMs);
  void (*prepareSleep)();
  uint8_t (*minimalLevel)();  // the minimal view's brightness, 0 = BRIGHTNESS_MINIMAL
};

void begin(const Hooks &hooks);
void update();  // every loop

// Input arrived (touch or button press). Returns true if the screen was
// already active, i.e. the input should act; false if it only woke it.
bool onPress();
void activity();         // ongoing input (e.g. a finger still moving)
void applyBrightness();  // the user brightness changed: show it now (no fade)
void requestSleep();     // long press: animation, wait for release, deep sleep

::power::Stage stage();

}  // namespace core::power
