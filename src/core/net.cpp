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
#include "rtc_clock.h"
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
static uint32_t scanAtMs;  // when the last scan finished (0: none, or stale)
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

// ------------------------------------------------------------ known networks

// The networks saved on the device (most recent first, each with what was
// learned last time), plus WIFI_SSID from secrets.h if it isn't saved yet.
// For WIFI_SSID the secrets.h password wins: that's the one edited on purpose.
static settings::WifiNet known_[settings::kWifiSaved + 1];
static int nKnown;
static void collectKnown() {
  nKnown = 0;
  bool haveSecret = false;
  for (int i = 0; i < settings::wifiCount(); i++) {
    settings::WifiNet &k = known_[nKnown++];
    k = settings::wifi(i);
    if (WIFI_SSID[0] && strcmp(k.ssid, WIFI_SSID) == 0) {
      strlcpy(k.password, WIFI_PASSWORD, sizeof k.password);
      haveSecret = true;
    }
  }
  if (!haveSecret && WIFI_SSID[0]) {
    settings::WifiNet &k = known_[nKnown++];
    k = {};
    strlcpy(k.ssid, WIFI_SSID, sizeof k.ssid);
    strlcpy(k.password, WIFI_PASSWORD, sizeof k.password);
  }
}
static int knownIndex(const char *ssid) {
  collectKnown();
  for (int i = 0; i < nKnown; i++)
    if (strcmp(known_[i].ssid, ssid) == 0) return i;
  return -1;
}
static bool configured() { return settings::wifiCount() > 0 || WIFI_SSID[0] != '\0'; }

// The network on the radio now (or last), and how it was joined.
static settings::WifiNet cur;
static bool curCached;     // on its remembered channel / access point
static bool curLeaseIp;    // with its remembered DHCP lease as a static IP
static bool fastTried;     // the last network's cached join was tried since the last connect
static uint32_t triedMask;  // known networks tried since the last scan, by index

static bool scanFresh(uint32_t now) { return scanAtMs && scanCount >= 0 && now - scanAtMs < WIFI_SCAN_FRESH_MS; }
static int32_t scannedRssi(const char *ssid) {
  for (int i = 0; i < scanCount; i++)
    if (strcmp(networks[i].ssid, ssid) == 0) return networks[i].rssi;
  return INT32_MIN;  // not in range
}

// A remembered lease is reused only while it's young enough that the router
// most likely still holds it for us, and only on the same access point.
static bool leaseUsable(const settings::WifiNet &n) {
  if (!n.ip || !n.leasedAt || !clock::valid()) return false;
  const time_t now = time(nullptr);
  return now >= time_t(n.leasedAt) && now - time_t(n.leasedAt) < WIFI_LEASE_REUSE_S;
}

// Picks the network to join into `cur`: the one on trial; else the last
// network, straight on its cached channel with its lease (no scan, no DHCP);
// else a single known network as is; else the strongest known network in a
// fresh scan not tried yet, scanning first if there is no fresh scan. Returns
// false when there is nothing to try now.
static bool pick(uint32_t now) {
  curCached = curLeaseIp = false;
  if (joinSt == Join::Trying) {
    cur = {};
    strlcpy(cur.ssid, joinSsid, sizeof cur.ssid);
    strlcpy(cur.password, joinPassword, sizeof cur.password);
    return true;
  }
  collectKnown();
  if (!nKnown) return false;
  if (!fastTried && known_[0].channel) {
    fastTried = true;
    cur = known_[0];
    curCached = true;
    curLeaseIp = leaseUsable(cur);
    return true;
  }
  int best = 0;
  if (nKnown > 1) {
    if (!scanFresh(now)) {
      startScan();  // the loop comes back here when it's done
      return false;
    }
    best = -1;
    int32_t bestRssi = INT32_MIN;
    for (int i = 0; i < nKnown; i++) {
      const int32_t rssi = scannedRssi(known_[i].ssid);
      if (!(triedMask & (1u << i)) && rssi > bestRssi) {
        best = i;
        bestRssi = rssi;
      }
    }
    if (best < 0) {  // none in range, or all tried: wait, then scan again
      ESP_LOGW(TAG, "no known network in range");
      triedMask = 0;
      scanAtMs = 0;
      fastTried = false;
      retryAtMs = now + WIFI_RETRY_MS;
      return false;
    }
    triedMask |= 1u << best;
  }
  cur = known_[best];
  return true;
}

// secrets.h can fix a static IP for WIFI_SSID; it beats a remembered lease.
static bool secretStaticIp(IPAddress &ip, IPAddress &gw, IPAddress &mask, IPAddress &dns) {
  if (!WIFI_SSID[0] || strcmp(cur.ssid, WIFI_SSID) != 0) return false;
  if (!ip.fromString(WIFI_STATIC_IP) || !gw.fromString(WIFI_GATEWAY) || !mask.fromString(WIFI_SUBNET)) return false;
  if (!dns.fromString(WIFI_DNS)) dns = gw;
  return true;
}
static bool curStaticSecret() {
  IPAddress ip, gw, mask, dns;
  return secretStaticIp(ip, gw, mask, dns);
}

// After a connect: remember the channel, access point and (when it came from
// DHCP) the lease for next time. NVS is written only when something changed.
static void learn() {
  settings::WifiNet n = cur;  // a known network's saved fields, or a trial's new password
  n.channel = uint8_t(WiFi.channel());
  memcpy(n.bssid, WiFi.BSSID(), sizeof n.bssid);
  if (!curLeaseIp && !curStaticSecret()) {
    n.ip = uint32_t(WiFi.localIP());
    n.gateway = uint32_t(WiFi.gatewayIP());
    n.mask = uint32_t(WiFi.subnetMask());
    n.dns = uint32_t(WiFi.dnsIP());
    n.leasedAt = clock::valid() ? uint32_t(time(nullptr)) : 0;
  }
  if (settings::wifiCount() && memcmp(&n, &settings::wifi(0), sizeof n) == 0) return;
  settings::saveWifi(n);
}

// The one place linkUp changes, so connected() is already current when an
// app handles the WifiChanged event (posted from the UI task, not the Wi-Fi
// event task).
static void setLink(bool up, uint32_t now) {
  if (up == linkUp) return;
  linkUp = up;
  if (up) {
    if (joinSt == Join::Trying) joinSt = Join::Joined;  // it works: learn() keeps it
    learn();
    triedMask = 0;
    fastTried = false;
    strlcpy(ipText, WiFi.localIP().toString().c_str(), sizeof ipText);
    ESP_LOGI(TAG, "connected to %s, %s, %d dBm, %lu ms%s%s", cur.ssid, ipText, WiFi.RSSI(),
             (unsigned long)(now - startedMs), curCached ? ", cached channel" : "", curLeaseIp ? ", reused lease" : "");
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

static void startRadio(uint32_t now) {
  if (!pick(now)) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  IPAddress ip, gw, mask, dns;
  if (secretStaticIp(ip, gw, mask, dns)) {
    WiFi.config(ip, gw, mask, dns);
  } else if (curLeaseIp) {
    WiFi.config(IPAddress(cur.ip), IPAddress(cur.gateway), IPAddress(cur.mask), IPAddress(cur.dns));
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP (clears a static IP from the last network)
  }
  if (curCached)
    WiFi.begin(cur.ssid, cur.password, cur.channel, cur.bssid, true);
  else
    WiFi.begin(cur.ssid, cur.password);
  radioOn = true;
  startedMs = now;
  lastReason = 0;
  ESP_LOGI(TAG, "connecting to %s%s%s", cur.ssid, curCached ? " (cached channel)" : "", curLeaseIp ? " (reused lease)" : "");
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
    scanAtMs = now ? now : 1;
    triedMask = 0;
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
    if (!radioOn && configured() && reached(now, retryAtMs)) startRadio(now);
    // A stale cached BSSID/channel can make the fast join fail: drop the
    // cache and retry with a full scan.
    const uint32_t timeout = joinSt == Join::Trying ? WIFI_JOIN_TIMEOUT_MS : WIFI_CONNECT_TIMEOUT_MS;
    if (radioOn && !linkUp && now - startedMs > timeout) {
      const bool trial = joinSt == Join::Trying;
      stopRadio(now);  // also ends a trial as Failed
      if (trial) {
        ESP_LOGW(TAG, "couldn't join %s; back to the saved network", joinSsid);
        retryAtMs = now;
      } else {
        ESP_LOGW(TAG, "connect to %s timed out", cur.ssid);
        // After a cached join, or with several known networks: the next try
        // right away (pick() waits once all are tried); else wait first.
        collectKnown();
        retryAtMs = curCached || nKnown > 1 ? now : now + WIFI_RETRY_MS;
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
  if (!password) {
    const int i = knownIndex(ssid);
    password = i >= 0 ? known_[i].password : "";
  }
  strlcpy(joinPassword, password, sizeof joinPassword);
  strlcpy(joinSsid, ssid, sizeof joinSsid);
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

const char *ssid() {
  if (joinSt == Join::Trying) return joinSsid;
  if (cur.ssid[0]) return cur.ssid;
  collectKnown();
  return nKnown ? known_[0].ssid : "";
}

bool known(const char *ssid) { return knownIndex(ssid) >= 0; }

bool connected() { return linkUp; }
int rssi() { return linkUp ? WiFi.RSSI() : 0; }
const char *ip() { return ipText; }

}  // namespace core::net
