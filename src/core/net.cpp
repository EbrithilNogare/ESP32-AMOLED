#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include "config.h"
#include "events.h"
#include "rtc_clock.h"
#include "store.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace core::net {

static const char *TAG = "net";
static constexpr uint32_t kGraceMs = 30000;

static int holders;
static bool radioOn;
static bool linkUp;
static uint32_t startedMs, releasedMs, retryAtMs;
static bool sntpStarted;
static char ipText[16];

static void onEvent(arduino_event_id_t id, arduino_event_info_t) {
  if (id == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    events::post({Ev::WifiChanged, 1, 0});
  else if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    events::post({Ev::WifiChanged, 0, 0});
}

static void onTimeSync(timeval *) { events::post({Ev::TimeSynced, 0, 0}); }

static void startRadio() {
  if (strlen(WIFI_SSID) == 0) return;
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

static void stopRadio() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  radioOn = false;
  linkUp = false;
  ipText[0] = 0;
}

void begin() {
  WiFi.onEvent(onEvent);
  WiFi.persistent(false);
}

void loop() {
  const uint32_t now = millis();
  const bool up = radioOn && WiFi.status() == WL_CONNECTED;
  if (up != linkUp) {
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
    }
  }

  if (holders > 0) {
    if (!radioOn && now >= retryAtMs) startRadio();
    // A stale cached BSSID/channel can make the fast join fail: drop the
    // cache and retry with a full scan.
    if (radioOn && !linkUp && now - startedMs > WIFI_CONNECT_TIMEOUT_MS) {
      ESP_LOGW(TAG, "connect timed out; retrying");
      store::rtc().wifiChannel = 0;
      stopRadio();
      retryAtMs = now + WIFI_RETRY_MS;
    }
  } else if (radioOn && now - releasedMs > kGraceMs) {
    stopRadio();
  }
}

void acquire() {
  if (holders++ == 0) retryAtMs = 0;
}

void release() {
  if (holders > 0 && --holders == 0) releasedMs = millis();
}

void shutdown() {
  if (radioOn) stopRadio();
}

bool connected() { return linkUp; }
int rssi() { return linkUp ? WiFi.RSSI() : 0; }
const char *ip() { return ipText; }

}  // namespace core::net
