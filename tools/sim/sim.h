#pragma once
#include <level_core.h>
#include <power_fsm.h>
#include <sleep_log.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "core/net.h"
#include "core/ota.h"
#include "ha_parse.h"
#include "touch_cal.h"

// Knobs the simulator script turns (see sim_core.cpp).
namespace sim {
extern uint32_t nowMs;
extern bool wifi;
extern int batteryPct;
extern sleeplog::Log sleepLog;  // what System shows as Standby
extern bool charger;
extern power::Stage stage;
extern core::ota::State otaState;
extern uint8_t otaProgress;
extern int haRefreshes;
extern int haListLoads;
extern std::vector<ha::LightInfo> haLoaded;  // what loadLights() "returns" (then onLights(Ok))
extern std::vector<int> haToggles;
extern level::Accel accel;                        // IMU reading (screen axes, g)
extern std::string ssid;                          // the network net joins
extern std::vector<core::net::Network> networks;  // scan results
extern bool scanDone;
extern int scans;
extern core::net::Join join;
extern std::string joinSsid, joinPassword;
extern int brightnessApplied;
extern int volumeApplied;
extern int noise;               // -1 silent, else the noise::Kind playing
extern std::vector<int> beeps;  // core::sound::Pattern of every beep played
extern bool radio;              // net::radioOn()
extern int wakes;               // core::power::wake() calls (an app wanted the screen)
extern int64_t wallSec;         // core::clock::wallMs() / 1000 (moved on by hand: "time away")
extern int64_t monoOffset;      // core::mono() = extend(millis) + this: a "deep sleep" moves it on
// Touch: the correction core::touch applies, raw mode, and a panel error to
// emulate (raw = (finger - offset) / scale per axis; identity by default).
extern touchcal::Cal touchCal;
extern bool touchRaw;
struct PanelError {
  double xScale = 1, xOffset = 0, yScale = 1, yOffset = 0;
};
extern PanelError panel;
}  // namespace sim
