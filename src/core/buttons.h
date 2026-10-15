#pragma once
#include "pmu.h"

// The two side buttons, mapped like a smartwatch: one is Back, the other Home
// (BUTTON_BACK_IS_BOOT picks which). BOOT is a GPIO debounced here; PWR goes
// through the AXP2101, which reports short and long presses as IRQs.
// Results are posted as Back / Home / LongPress events.
namespace core::buttons {

// `onPress` is asked when a press starts; false means the press only woke the
// screen, so its short-press action is dropped (a long press still counts).
void begin(bool (*onPress)());
// After a wake by BOOT the button may still be held: ignore that press
// entirely, so it doesn't count as a new short or long press.
void ignoreHeldPress();
void poll(const pmu::KeyEvents &pwr);  // call every loop (~5 ms)
bool bootHeld();

}  // namespace core::buttons
