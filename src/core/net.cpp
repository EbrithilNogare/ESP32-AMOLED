#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include "config.h"
#include "events.h"
#include "secrets_or_example.h"
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

static void onTimeSync(timeval *) { events::post({Ev::TimeSynced, 0, 0}); }

static bool configured() { return WIFI_SSID[0] != '\0'; }

// The one place linkUp changes, so connected() is already current when an
// app handles the WifiChanged event (posted from the UI task, not the Wi-Fi
// event task).
static void setLink(bool up, uint32_t now) {
  if (up == linkUp) return;
  linkUp = up;
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
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, rtc.wifiChannel, rtc.wifiBssid, true);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  radioOn = true;
  startedMs = millis();
  ESP_LOGI(TAG, "connecting to %s", WIFI_SSID);
}

static void stopRadio(uint32_t now) {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  radioOn = false;
  setLink(false, now);
}

void begin() { WiFi.persistent(false); }

void loop() {
  const uint32_t now = millis();
  setLink(radioOn && WiFi.status() == WL_CONNECTED, now);

  if (suspended) {
    if (radioOn) stopRadio(now);
  } else if (holders > 0) {
    if (!radioOn && configured() && reached(now, retryAtMs)) startRadio();
    // A stale cached BSSID/channel can make the fast join fail: drop the
    // cache and retry with a full scan.
    if (radioOn && !linkUp && now - startedMs > WIFI_CONNECT_TIMEOUT_MS) {
      ESP_LOGW(TAG, "connect timed out; retrying");
      store::rtc().wifiChannel = 0;
      stopRadio(now);
      retryAtMs = now + WIFI_RETRY_MS;
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

bool connected() { return linkUp; }
int rssi() { return linkUp ? WiFi.RSSI() : 0; }
const char *ip() { return ipText; }

}  // namespace core::net
