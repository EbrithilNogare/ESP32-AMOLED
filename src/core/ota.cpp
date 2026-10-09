#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "net.h"
#include "pmu.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// The Arduino core confirms a new OTA image during startup unless told to
// wait; we wait until the UI has proven itself (healthTick()). The hook is a
// weak C symbol in esp32-hal-misc.c, hence extern "C".
extern "C" bool verifyRollbackLater() { return true; }

namespace core::ota {

static const char *TAG = "ota";
static State st = State::Off;
static uint8_t pct;
static const char *err = "";
static bool pendingVerify;
static bool listening;
static bool netHeld;
static void (*progressHook)();
static uint32_t lastHookMs;

void begin() {
  esp_ota_img_states_t s;
  pendingVerify =
      esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) == ESP_OK && s == ESP_OTA_IMG_PENDING_VERIFY;
  if (pendingVerify) ESP_LOGW(TAG, "new firmware: confirming after %d ms of healthy uptime", OTA_HEALTHY_AFTER_MS);
}

void healthTick() {
  if (pendingVerify && millis() > OTA_HEALTHY_AFTER_MS) {
    esp_ota_mark_app_valid_cancel_rollback();
    pendingVerify = false;
    ESP_LOGI(TAG, "firmware confirmed");
  }
}

static void startListener() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setRebootOnSuccess(false);  // the UI shows "Done" first, then restarts
  ArduinoOTA.onStart([]() {
    st = State::Receiving;
    pct = 0;
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    if (total) pct = uint8_t(done * 100ULL / total);
    if (progressHook && millis() - lastHookMs >= 100) {
      lastHookMs = millis();
      progressHook();
    }
  });
  ArduinoOTA.onEnd([]() {
    st = State::Done;
    pct = 100;
  });
  ArduinoOTA.onError([](ota_error_t e) {
    st = State::Error;
    err = e == OTA_AUTH_ERROR      ? "Wrong password"
          : e == OTA_BEGIN_ERROR   ? "Image too big"
          : e == OTA_CONNECT_ERROR ? "Connection failed"
          : e == OTA_RECEIVE_ERROR ? "Transfer interrupted"
                                   : "Update failed";
    ESP_LOGE(TAG, "%s", err);
  });
  ArduinoOTA.begin();
  WiFi.setSleep(false);  // modem sleep makes the transfer crawl
  listening = true;
  st = State::Waiting;
  ESP_LOGI(TAG, "listening as %s.local", OTA_HOSTNAME);
}

bool start() {
  if (strlen(OTA_PASSWORD) == 0) {
    err = "Set OTA_PASSWORD in secrets.h";
    st = State::Error;
    return false;
  }
  const int bat = pmu::batteryPercent();
  if (!pmu::onCharger() && bat >= 0 && bat < OTA_MIN_BATTERY_PCT) {
    err = "Battery too low (charge first)";
    st = State::Error;
    return false;
  }
  if (!netHeld) {
    net::acquire();
    netHeld = true;
  }
  err = "";
  st = State::WaitingForWifi;
  return true;
}

void stop() {
  if (listening) {
    ArduinoOTA.end();
    listening = false;
  }
  if (netHeld) {
    net::release();
    netHeld = false;
  }
  if (st != State::Done) st = State::Off;
}

void loop() {
  if (st == State::WaitingForWifi && net::connected()) startListener();
  if (listening) ArduinoOTA.handle();
}

void setProgressHook(void (*hook)()) { progressHook = hook; }

State state() { return st; }
uint8_t progress() { return pct; }
const char *error() { return err; }

}  // namespace core::ota
