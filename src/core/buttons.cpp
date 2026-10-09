#include "buttons.h"

#include <Arduino.h>
#include <button_logic.h>

#include "board.h"
#include "config.h"
#include "events.h"

namespace core::buttons {

static bool (*pressHook)();
static btn::Button boot({BUTTON_DEBOUNCE_MS, BUTTON_LONG_PRESS_MS});
static bool bootSwallow;
static bool ignoreFirst;

static constexpr Ev kBootEv = BUTTON_BACK_IS_BOOT ? Ev::Back : Ev::Home;
static constexpr Ev kPwrEv = BUTTON_BACK_IS_BOOT ? Ev::Home : Ev::Back;

void begin(bool (*onPress)()) {
  pressHook = onPress;
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

void poll(const pmu::KeyEvents &pwr) {
  switch (boot.update(digitalRead(PIN_BOOT_BUTTON) == LOW, millis())) {
    case btn::Event::Pressed:
      if (ignoreFirst && millis() < 2000) {
        boot.suppress();  // cancels Short and Long for this press
        bootSwallow = true;
      } else {
        bootSwallow = pressHook && !pressHook();
      }
      ignoreFirst = false;
      break;
    case btn::Event::Short:
      if (!bootSwallow) events::post({kBootEv, 0, 0});
      break;
    case btn::Event::Long: events::post({Ev::LongPress, 0, 0}); break;
    default: break;
  }

  // The AXP2101 reports PWR presses only once they're classified (short on
  // release, long after 1 s), so "press started" and "action" coincide.
  if (pwr.longPress) {
    if (pressHook) pressHook();
    events::post({Ev::LongPress, 0, 0});
  } else if (pwr.shortPress) {
    if (!pressHook || pressHook()) events::post({kPwrEv, 0, 0});
  }
}

void ignoreHeldPress() { ignoreFirst = true; }

bool bootHeld() { return boot.held() || digitalRead(PIN_BOOT_BUTTON) == LOW; }

}  // namespace core::buttons
