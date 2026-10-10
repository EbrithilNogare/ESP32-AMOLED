// Host stand-ins for the hardware modules the apps talk to. The real headers
// from src/core are used, so the apps compile unchanged; tools/sim/sim_main.cpp
// drives these values to put each screen into the state it wants to capture.
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "apps/home_assistant/ha_client.h"
#include "config.h"
#include "core/board_init.h"
#include "core/imu.h"
#include "core/net.h"
#include "core/ota.h"
#include "core/pmu.h"
#include "core/power.h"
#include "core/rtc_clock.h"
#include "core/settings.h"
#include "core/sound.h"
#include "core/store.h"
#include "sim.h"

namespace sim {
// Starts 30 s before millis() wraps, so the flow test runs across the wrap.
uint32_t nowMs = UINT32_MAX - 30000;
bool wifi = true;
int batteryPct = 82;
bool charger = false;
power::Stage stage = power::Stage::Active;
core::ota::State otaState = core::ota::State::Waiting;
uint8_t otaProgress = 0;
int haRefreshes = 0;
std::vector<int> haToggles;
level::Accel accel{0, 0, 1};
std::string ssid = "HomeNet";
std::vector<core::net::Network> networks;
bool scanDone = true;
int scans = 0;
core::net::Join join = core::net::Join::Idle;
std::string joinSsid, joinPassword;
int brightnessApplied = 0;
int volumeApplied = 0;
int noise = -1;
std::vector<int> beeps;
}  // namespace sim

SimEsp ESP;

namespace core {
namespace net {
void begin() {}
void loop() {}
void acquire() {}
void release() {}
void shutdown() {}
bool connected() { return sim::wifi; }
int rssi() { return sim::wifi ? -61 : 0; }
const char *ip() { return sim::wifi ? "192.168.1.57" : ""; }
const char *ssid() { return sim::ssid.c_str(); }
void startScan() { sim::scans++; }
int scanResults(const Network *&list) {
  list = sim::networks.data();
  return sim::scanDone ? int(sim::networks.size()) : -1;
}
void join(const char *ssid, const char *password) {
  sim::joinSsid = ssid;
  sim::joinPassword = password;
  sim::join = Join::Trying;
}
Join joinState() { return sim::join; }
}  // namespace net

namespace pmu {
bool onCharger() { return sim::charger; }
bool charging() { return sim::charger; }
int batteryPercent() { return sim::batteryPct; }
uint16_t batteryMillivolts() { return 3950; }
}  // namespace pmu

namespace clock {
bool valid() { return true; }
}  // namespace clock

namespace power {
::power::Stage stage() { return sim::stage; }
void activity() {}
bool onPress() { return true; }
void applyBrightness() { sim::brightnessApplied++; }
}  // namespace power

namespace sound {
void play(Pattern p) {
  if (core::settings::muted()) return;
  sim::beeps.push_back(int(p));
}
void applyVolume() { sim::volumeApplied++; }
void startNoise(noise::Kind kind) { sim::noise = int(kind); }
void stopNoise() { sim::noise = -1; }
bool noisePlaying() { return sim::noise >= 0; }
bool busy() { return sim::noise >= 0; }
}  // namespace sound

namespace board {
const char *revisionName() { return "V2"; }
}  // namespace board

namespace store {
static RtcState rtcState;
static std::map<std::string, std::vector<uint8_t>> nvs;
RtcState &rtc() { return rtcState; }
bool rtcValid() { return false; }
void rtcCommit() {}
bool load(const char *key, void *data, size_t len) {
  auto it = nvs.find(key);
  if (it == nvs.end() || it->second.size() != len) return false;
  memcpy(data, it->second.data(), len);
  return true;
}
bool save(const char *key, const void *data, size_t len) {
  nvs[key].assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + len);
  return true;
}
}  // namespace store

namespace imu {
bool begin() { return true; }
bool present() { return true; }
void enable(bool) {}
bool read(level::Accel &out) {
  out = sim::accel;
  return true;
}
}  // namespace imu

namespace ota {
bool start() { return true; }
void stop() {}
State state() { return sim::otaState; }
uint8_t progress() { return sim::otaProgress; }
const char *error() { return "Wrong password"; }
}  // namespace ota
}  // namespace core

namespace apps::ha_client {
static const Light kTable[kLights] = HA_LIGHTS;
const Light &light(int i) { return kTable[i]; }
void begin() {}
bool refreshAll() {
  sim::haRefreshes++;
  return true;
}
bool toggle(int i) {
  sim::haToggles.push_back(i);
  return true;
}
bool busy() { return false; }
}  // namespace apps::ha_client
