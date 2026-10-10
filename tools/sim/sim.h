#pragma once
#include <level_core.h>
#include <power_fsm.h>
#include <stdint.h>

#include <vector>

#include "core/ota.h"

// Knobs the simulator script turns (see sim_core.cpp).
namespace sim {
extern uint32_t nowMs;
extern bool wifi;
extern int batteryPct;
extern bool charger;
extern power::Stage stage;
extern core::ota::State otaState;
extern uint8_t otaProgress;
extern int haRefreshes;
extern std::vector<int> haToggles;
extern level::Accel accel;  // IMU reading (screen axes, g)
}  // namespace sim
