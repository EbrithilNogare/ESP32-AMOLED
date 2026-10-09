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
  bool (*enterMinimal)();
  void (*exitMinimal)();
  void (*minimalTick)(uint32_t nowMs);
  uint32_t (*minimalRefreshMs)(uint32_t nowMs);
  void (*prepareSleep)();
};

void begin(const Hooks &hooks);
void update();  // every loop

// Input arrived (touch or button press). Returns true if the screen was
// already active, i.e. the input should act; false if it only woke it.
bool onPress();
void activity();      // ongoing input (e.g. a finger still moving)
void requestSleep();  // long press: animation, wait for release, deep sleep

::power::Stage stage();
::power::Profile profile();
uint32_t idleMs();

}  // namespace core::power
