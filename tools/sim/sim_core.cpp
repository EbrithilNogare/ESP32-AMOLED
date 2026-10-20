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
#include "core/ticks.h"
#include "core/touch.h"
#include "sim.h"

namespace sim {
// Starts 30 s before millis() wraps, so the flow test runs across the wrap.
uint32_t nowMs = UINT32_MAX - 30000;
bool wifi = true;
int batteryPct = 82;
sleeplog::Log sleepLog = [] {
  sleeplog::Log l;  // two nights on battery: 4 % in 52 h
  l.totalS = 52 * 3600;
  l.totalLost = 4;
  return l;
}();
bool charger = false;
power::Stage stage = power::Stage::Active;
core::ota::State otaState = core::ota::State::Waiting;
uint8_t otaProgress = 0;
int haRefreshes = 0;
int haListLoads = 0;
std::vector<ha::LightInfo> haLoaded;
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
bool radio = true;
int wakes = 0;
int64_t monoOffset = 0;
int64_t wallSec = 1792000000;  // October 2026
touchcal::Cal touchCal;
bool touchRaw = false;
PanelError panel;
}  // namespace sim

SimEsp ESP;

namespace core {
uint64_t mono(uint32_t ms) { return uint64_t(int64_t(extend(ms)) + sim::monoOffset); }
}  // namespace core

namespace core {
namespace net {
void begin() {}
void loop() {}
void acquire() {}
void release() {}
void shutdown() {}
bool connected() { return sim::wifi; }
bool radioOn() { return sim::radio; }
int rssi() { return sim::wifi ? -61 : 0; }
const char *ip() { return sim::wifi ? "192.168.1.57" : ""; }
const char *ssid() { return sim::ssid.c_str(); }
void startScan() { sim::scans++; }
int scanResults(const Network *&list) {
  list = sim::networks.data();
  return sim::scanDone ? int(sim::networks.size()) : -1;
}
bool known(const char *ssid) {
  if (sim::ssid == ssid) return true;
  for (int i = 0; i < settings::wifiCount(); i++)
    if (strcmp(settings::wifi(i).ssid, ssid) == 0) return true;
  return false;
}
void join(const char *ssid, const char *password) {
  sim::joinSsid = ssid;
  sim::joinPassword = password ? password : "(saved)";
  sim::join = Join::Trying;
}
Join joinState() { return sim::join; }
JoinFail joinFail() { return sim::join == Join::Failed ? JoinFail::Password : JoinFail::None; }
uint8_t joinReason() { return sim::join == Join::Failed ? 15 : 0; }
}  // namespace net

namespace touch {
void setCalibration(const touchcal::Cal &c) { sim::touchCal = c; }
void setRaw(bool on) { sim::touchRaw = on; }
}  // namespace touch

namespace pmu {
bool onCharger() { return sim::charger; }
bool charging() { return sim::charger; }
int batteryPercent() { return sim::batteryPct; }
uint16_t batteryMillivolts() { return 3950; }
}  // namespace pmu

namespace clock {
bool valid() { return true; }
int64_t wallMs() { return sim::wallSec * 1000 + sim::nowMs % 1000; }
}  // namespace clock

namespace power {
::power::Stage stage() { return sim::stage; }
void activity() {}
bool onPress() { return true; }
void wake() {
  sim::wakes++;
  sim::stage = ::power::Stage::Active;
}
void applyBrightness() { sim::brightnessApplied++; }
const sleeplog::Log &sleepLog() { return sim::sleepLog; }
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
static std::map<std::string, std::vector<uint8_t>> parked;
RtcState &rtc() { return rtcState; }
bool rtcValid() { return rtcState.magic != 0; }
void rtcCommit() { rtcState.magic = 1; }
bool park(const char *key, const void *data, size_t len) {
  parked[key].assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + len);
  return true;
}
bool unpark(const char *key, void *data, size_t len) {
  auto it = parked.find(key);
  if (it == parked.end() || it->second.size() != len) return false;
  memcpy(data, it->second.data(), len);
  return true;
}
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
static ha::LightInfo list[kMaxLights];
static int n;
static uint8_t gen;
void begin() {}
bool loadLights() {
  sim::haListLoads++;
  return true;
}
bool refresh() {
  sim::haRefreshes++;
  return true;
}
bool toggle(int i) {
  sim::haToggles.push_back(i);
  return true;
}
bool busy() { return false; }
int count() { return n; }
const ha::LightInfo &light(int i) { return list[i]; }
uint8_t generation() { return gen; }
void setList(const ha::LightInfo *l, int count) {
  n = count;
  for (int i = 0; i < n; i++) list[i] = l[i];
  gen++;
}
bool adopt(ha::Result r) {
  if (r != ha::Result::Ok) return false;
  bool changed = int(sim::haLoaded.size()) != n;
  for (int i = 0; !changed && i < n; i++) changed = strcmp(list[i].entity, sim::haLoaded[size_t(i)].entity) != 0;
  n = int(sim::haLoaded.size());
  for (int i = 0; i < n; i++) list[i] = sim::haLoaded[size_t(i)];
  if (changed) gen++;
  return changed;
}
}  // namespace apps::ha_client
