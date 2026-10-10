#pragma once
#include <level_core.h>
#include <power_fsm.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "core/net.h"
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
extern level::Accel accel;                        // IMU reading (screen axes, g)
extern std::string ssid;                          // the network net joins
extern std::vector<core::net::Network> networks;  // scan results
extern bool scanDone;
extern int scans;
extern core::net::Join join;
extern std::string joinSsid, joinPassword;
extern int brightnessApplied;
}  // namespace sim
