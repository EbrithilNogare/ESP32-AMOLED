#include "buttons.h"

#include <Arduino.h>
#include <button_logic.h>

#include "board.h"
#include "config.h"
#include "events.h"

namespace core::buttons {

static bool (*powerHook)();
static bool (*navHook)();
static btn::Button boot({BUTTON_DEBOUNCE_MS, BUTTON_POWER_OFF_MS});
static bool ignoreFirst;

void begin(bool (*onPower)(), bool (*onNav)()) {
  powerHook = onPower;
  navHook = onNav;
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

void poll(const pmu::KeyEvents &pwr) {
  switch (boot.update(digitalRead(PIN_BOOT_BUTTON) == LOW, millis())) {
    case btn::Event::Pressed: {
      const bool wakePress = ignoreFirst && millis() < BUTTON_WAKE_IGNORE_MS;  // uptime, not a deadline
      ignoreFirst = false;
      // A press that only wakes the screen must not power off when held a
      // little long either: suppress cancels both Short and Long.
      if (wakePress || (powerHook && !powerHook())) boot.suppress();
      break;
    }
    case btn::Event::Short: events::post({Ev::Power, 0, 0}); break;
    case btn::Event::Long: events::post({Ev::PowerOff, 0, 0}); break;
    default: break;
  }

  // The AXP2101 reports PWR presses only once they're classified (short on
  // release, long after BUTTON_LONG_PRESS_MS, see pmu.cpp), so "press
  // started" and "action" coincide.
  if (pwr.longPress) {
    if (!navHook || navHook()) events::post({Ev::Home, 0, 0});
  } else if (pwr.shortPress) {
    if (!navHook || navHook()) events::post({Ev::Back, 0, 0});
  }
}

// Only if BOOT is still down: a wake press already released must not eat the
// next real press.
void ignoreHeldPress() { ignoreFirst = digitalRead(PIN_BOOT_BUTTON) == LOW; }

bool bootHeld() { return boot.held() || digitalRead(PIN_BOOT_BUTTON) == LOW; }

}  // namespace core::buttons
