#pragma once
#include "pmu.h"

// The two side buttons, mapped like a phone (ARCHITECTURE §2.2):
// - BOOT is Power: a press turns the screen off (Ev::Power), holding it
//   powers off (Ev::PowerOff). It's the one that can wake the chip (GPIO0).
// - PWR is Back (press, Ev::Back) and Home (hold, Ev::Home). It reaches the
//   ESP only through the AXP2101, which reports short and long presses.
// BOOT is a GPIO debounced here; PWR presses come from the PMU poll.
namespace core::buttons {

// Asked when a press starts; false means the press only woke the screen,
// so the press does nothing else (neither its short nor its long action).
// `onPower` is asked for BOOT, `onNav` for PWR.
void begin(bool (*onPower)(), bool (*onNav)());
// After a wake by BOOT the button may still be held: ignore that press
// entirely, so it doesn't count as a new short or long press.
void ignoreHeldPress();
void poll(const pmu::KeyEvents &pwr);  // call every loop (~5 ms)
bool bootHeld();

}  // namespace core::buttons
