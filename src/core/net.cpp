#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include <algorithm>

#include "config.h"
#include "events.h"
#include "secrets_or_example.h"
#include "settings.h"
#include "store.h"
#include "ticks.h"

namespace core::net {

[[maybe_unused]] static const char *TAG = "net";

static int holders;
static bool radioOn;
static bool linkUp;
static bool suspended;
static uint32_t startedMs, releasedMs, retryAtMs;
static bool sntpStarted;
static char ipText[16];
static bool scanning;
static int scanCount = -1;
static Network networks[kMaxNetworks];
static Join joinSt = Join::Idle;
static char joinSsid[settings::kSsidMax + 1], joinPassword[settings::kPasswordMax + 1];
static volatile uint8_t lastReason;  // last STA disconnect reason, written by the Wi-Fi event task
static uint8_t failReason;           // lastReason when a join on trial failed

static JoinFail classify(uint8_t reason) {
  switch (reason) {
    case 0: return JoinFail::Timeout;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE: return JoinFail::Password;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return JoinFail::NotFound;
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return JoinFail::Security;
    default: return JoinFail::Other;
  }
}

static void onTimeSync(timeval *) { events::post({Ev::TimeSynced, 0, 0}); }

// A network being tried comes first, then the one saved on the device, then
// the one compiled in.
static const char *wifiSsid() {
  return joinSt == Join::Trying ? joinSsid : settings::hasWifi() ? settings::wifiSsid() : WIFI_SSID;
}
static const char *wifiPassword() {
  return joinSt == Join::Trying ? joinPassword : settings::hasWifi() ? settings::wifiPassword() : WIFI_PASSWORD;
}
static bool configured() { return wifiSsid()[0] != '\0'; }

// The one place linkUp changes, so connected() is already current when an
// app handles the WifiChanged event (posted from the UI task, not the Wi-Fi
// event task).
static void setLink(bool up, uint32_t now) {
  if (up == linkUp) return;
  linkUp = up;
  if (up && joinSt == Join::Trying) {
    settings::setWifi(joinSsid, joinPassword);  // it works: keep it
    joinSt = Join::Joined;
  }
  if (up) {
    strlcpy(ipText, WiFi.localIP().toString().c_str(), sizeof ipText);
    store::RtcState &rtc = store::rtc();
    rtc.wifiChannel = uint8_t(WiFi.channel());
    memcpy(rtc.wifiBssid, WiFi.BSSID(), 6);
    store::rtcCommit();
    ESP_LOGI(TAG, "connected, %s, %d dBm, %lu ms", ipText, WiFi.RSSI(), (unsigned long)(now - startedMs));
    if (!sntpStarted) {
      sntp_set_time_sync_notification_cb(onTimeSync);
      configTzTime(TIMEZONE, NTP_SERVER);
      sntpStarted = true;
    }
  } else {
    ipText[0] = 0;
    // An established link dropped: give auto-reconnect a full connect
    // timeout before the loop tears the radio down and retries.
    startedMs = now;
    ESP_LOGI(TAG, "disconnected");
  }
  events::post({Ev::WifiChanged, up ? 1u : 0u, 0});
}

static void startRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  IPAddress ip, gw, mask, dns;
  if (ip.fromString(WIFI_STATIC_IP) && gw.fromString(WIFI_GATEWAY) && mask.fromString(WIFI_SUBNET)) {
    if (!dns.fromString(WIFI_DNS)) dns = gw;
    WiFi.config(ip, gw, mask, dns);
  }
  // After deep sleep, joining the cached channel/BSSID skips the scan.
  store::RtcState &rtc = store::rtc();
  if (store::rtcValid() && rtc.wifiChannel) {
    WiFi.begin(wifiSsid(), wifiPassword(), rtc.wifiChannel, rtc.wifiBssid, true);
  } else {
    WiFi.begin(wifiSsid(), wifiPassword());
  }
  radioOn = true;
  startedMs = millis();
  lastReason = 0;
  ESP_LOGI(TAG, "connecting to %s", wifiSsid());
}

static void stopRadio(uint32_t now) {
  if (scanning) {  // cut short (suspend, shutdown): report an empty result
    WiFi.scanDelete();
    scanning = false;
    scanCount = 0;
  }
  // A network on trial that loses its radio before connecting has failed;
  // the saved one is used again from here on.
  if (joinSt == Join::Trying) {
    joinSt = Join::Failed;
    failReason = lastReason;  // before our own disconnect overwrites it
    ESP_LOGE(TAG, "join %s failed after %lu ms, reason %u (%s)", joinSsid, (unsigned long)(now - startedMs),
             failReason, failReason ? WiFi.STA.disconnectReasonName(wifi_err_reason_t(failReason)) : "no answer");
  }
  // Otherwise the core's reconnect handler answers our own disconnect by
  // reconnecting a driver that is already stopped (STA disconnect failed /
  // STA config failed in the log).
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  radioOn = false;
  setLink(false, now);
}

// Keeps one entry per name (the strongest), strongest first.
static void collectScan(int found) {
  scanCount = 0;
  for (int i = 0; i < found; i++) {
    const String name = WiFi.SSID(i);
    if (name.isEmpty()) continue;  // hidden network
    const int8_t rssi = int8_t(WiFi.RSSI(i));
    int k = 0;
    while (k < scanCount && strcmp(networks[k].ssid, name.c_str()) != 0) k++;
    if (k < scanCount) {
      if (rssi > networks[k].rssi) networks[k].rssi = rssi;
      continue;
    }
    if (scanCount == kMaxNetworks) continue;
    Network &n = networks[scanCount++];
    strlcpy(n.ssid, name.c_str(), sizeof n.ssid);
    n.rssi = rssi;
    n.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  std::sort(networks, networks + scanCount, [](const Network &a, const Network &b) { return a.rssi > b.rssi; });
}

void begin() {
  WiFi.persistent(false);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) { lastReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void loop() {
  const uint32_t now = millis();
  setLink(radioOn && WiFi.status() == WL_CONNECTED, now);

  if (scanning) {
    const int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) return;  // leave the radio alone meanwhile
    scanning = false;
    collectScan(found > 0 ? found : 0);
    ESP_LOGI(TAG, "scan: %d networks", scanCount);
    // A connection attempt was paused (or the radio started just for the
    // scan): start over, so the join below runs normally again.
    if (!linkUp) {
      stopRadio(now);
      retryAtMs = now;
    }
  }

  if (suspended) {
    if (radioOn) stopRadio(now);
  } else if (holders > 0) {
    if (!radioOn && configured() && reached(now, retryAtMs)) startRadio();
    // A stale cached BSSID/channel can make the fast join fail: drop the
    // cache and retry with a full scan.
    const uint32_t timeout = joinSt == Join::Trying ? WIFI_JOIN_TIMEOUT_MS : WIFI_CONNECT_TIMEOUT_MS;
    if (radioOn && !linkUp && now - startedMs > timeout) {
      const bool trial = joinSt == Join::Trying;
      store::rtc().wifiChannel = 0;
      stopRadio(now);  // also ends a trial as Failed
      if (trial) {
        ESP_LOGW(TAG, "couldn't join %s; back to the saved network", joinSsid);
        retryAtMs = now;
      } else {
        ESP_LOGW(TAG, "connect timed out; retrying");
        retryAtMs = now + WIFI_RETRY_MS;
      }
    }
  } else if (radioOn && now - releasedMs > WIFI_GRACE_MS) {
    stopRadio(now);
  }
}

void acquire() {
  if (holders++ == 0) retryAtMs = millis();  // connect right away
}

void release() {
  if (holders > 0 && --holders == 0) releasedMs = millis();
}

void shutdown() {
  if (radioOn) stopRadio(millis());
}

void suspend(bool on) {
  if (on == suspended) return;
  suspended = on;
  if (on)
    shutdown();
  else
    retryAtMs = millis();  // holders get the radio back right away
}

void join(const char *ssid, const char *password) {
  strlcpy(joinSsid, ssid, sizeof joinSsid);
  strlcpy(joinPassword, password ? password : "", sizeof joinPassword);
  store::rtc().wifiChannel = 0;  // the cached channel / BSSID belong to the old network
  if (radioOn) stopRadio(millis());
  joinSt = Join::Trying;
  retryAtMs = millis();
}

Join joinState() { return joinSt; }
JoinFail joinFail() { return joinSt == Join::Failed ? classify(failReason) : JoinFail::None; }
uint8_t joinReason() { return joinSt == Join::Failed ? failReason : 0; }

void startScan() {
  if (scanning) return;
  if (suspended) {
    scanCount = 0;
    return;
  }
  if (!radioOn) {
    WiFi.mode(WIFI_STA);
    radioOn = true;
    startedMs = millis();
  } else if (!linkUp) {
    WiFi.disconnect(false, false);  // the driver can't scan while it's joining
  }
  scanCount = -1;
  scanning = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;
  if (!scanning) scanCount = 0;
}

int scanResults(const Network *&list) {
  list = networks;
  return scanning ? -1 : scanCount;
}

const char *ssid() { return wifiSsid(); }

bool connected() { return linkUp; }
int rssi() { return linkUp ? WiFi.RSSI() : 0; }
const char *ip() { return ipText; }

}  // namespace core::net
