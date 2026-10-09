#pragma once
#include <stdint.h>

// AXP2101 power management: charger/VBUS state, battery level, the PWR key and
// shutdown. IRQ flags are polled over I2C (the IRQ line only reaches the ESP
// through the IO expander, which is also I2C).
namespace core::pmu {

struct KeyEvents {
  bool shortPress;
  bool longPress;
};

bool begin();
KeyEvents poll();  // call every ~50 ms; posts VbusChanged events itself
bool onCharger();  // VBUS present
bool charging();
int batteryPercent();  // -1 if no battery
uint16_t batteryMillivolts();
bool pwrHeld();  // PWR currently pressed (EXIO4, per vendor demo)

}  // namespace core::pmu
