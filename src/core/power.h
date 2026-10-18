#pragma once
#include <power_fsm.h>
#include <sleep_log.h>
#include <sleep_plan.h>
#include <stdint.h>

// PowerManager (ARCHITECTURE §3, §8), like a phone's:
// - The screen dims, then goes Off after the idle timeout or a press of the
//   power button. Off means panel and touch asleep; then the apps decide how
//   deep the chip sleeps (lib/SleepPlan): it dozes while sound plays or on
//   the charger, else it deep-sleeps, with a timer wake when an app has a
//   deadline (a countdown). A timer wake boots with the screen dark and goes
//   back to sleep unless an app calls wake().
// - Holding the power button powers off: deep sleep that only the button
//   ends, everything stopped.
// - Apps that keep a minimal view (LowPowerDisplay) get it instead of Off
//   after the dim timeout, light-sleeping between redraws.
namespace core::power {

// How this boot started (read once, early in setup()).
enum class Boot : uint8_t {
  Cold,    // power-on, reset, flashing
  Button,  // the power button woke a deep sleep
  Timer,   // an app's deadline woke a deep sleep: start with the screen off
};
Boot bootCause();

struct Hooks {
  ::power::Policy (*policy)();
  uint32_t (*idleDimMs)();  // app override of the dim delay, 0 = default
  bool (*enterMinimal)();   // false: no app has a minimal view (the screen goes off instead)
  void (*exitMinimal)();
  void (*minimalTick)(uint32_t nowMs);
  uint32_t (*minimalRefreshMs)(uint32_t nowMs);
  uint8_t (*minimalLevel)();                       // the minimal view's brightness, 0 = BRIGHTNESS_MINIMAL
  sleepplan::Need (*sleepNeed)(uint64_t monoNow);  // what the apps need with the screen off
  void (*prepareSleep)(sleepplan::Kind kind);      // deep sleep is coming: persist
  void (*screenChanged)(bool on);                  // the screen went off / came back
};

void begin(const Hooks &hooks, Boot boot);
void update();  // every loop

// Touch or PWR pressed. True if the input should act; false if it only woke
// the screen (dimmed, minimal view) or the screen is off (then it's ignored:
// only the power button wakes).
bool onPress();
// The power button pressed. False if it woke the screen (off, minimal view);
// true if the press acts (its release turns the screen off).
bool onPowerPress();
void activity();         // ongoing input (e.g. a finger still moving)
void applyBrightness();  // the user brightness changed: show it now (no fade)
void sleepNow();         // power button press: animation, screen off
void powerOff();         // power button held: shutdown animation, deep sleep until the button
void wake();             // an app needs the user now (an alarm): screen on
bool dozing();           // the screen is off and the chip is up: the loop may run slower

::power::Stage stage();
const sleeplog::Log &sleepLog();  // battery drain while asleep, since the last charge

}  // namespace core::power
