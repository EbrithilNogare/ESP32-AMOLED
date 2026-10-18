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
KeyEvents poll();  // call every PMU_POLL_MS; also tracks VBUS and the battery level
bool onCharger();  // VBUS present
bool charging();
int batteryPercent();  // -1 if no battery
uint16_t batteryMillivolts();
void refreshLevel();  // read the level now instead of waiting for the next poll
// Before deep sleep: the voltage ADCs off (begin() turns them on again). The
// fuel gauge keeps counting.
void prepareSleep();

}  // namespace core::pmu
