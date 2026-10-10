#include "ha_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>

#include "config.h"
#include "core/events.h"
#include "core/net.h"
#include "secrets_or_example.h"

namespace apps::ha_client {

[[maybe_unused]] static const char *TAG = "ha";  // ESP_LOGD compiles out in release
static const Light kLightTable[kLights] = HA_LIGHTS;
static_assert(sizeof(kLightTable) / sizeof(kLightTable[0]) == kLights, "HA_LIGHTS must list exactly 4 lights");

enum class Op : uint8_t { Get, Toggle };
struct Request {
  Op op;
  uint8_t index;
};

static QueueHandle_t queue;
static std::atomic<int> pending{0};  // queued or running; counted up before the send

const Light &light(int i) { return kLightTable[i]; }

// `toggle`: the result answers a tap (also when a GET had to follow the POST).
static void report(int i, ha::LightState s, ha::Result r, bool toggle) {
  core::events::post({core::Ev::HaResult, uint32_t(i), pack(s, r, toggle)});
}

static void run(HTTPClient &http, WiFiClient &client, const Request &rq, bool toggle) {
  const Light &l = kLightTable[rq.index];
  if (!core::net::connected()) {
    report(rq.index, ha::LightState::Unknown, ha::Result::NetworkError, toggle);
    return;
  }
  String url = String(HA_BASE_URL) + (rq.op == Op::Get ? "/api/states/" : "/api/services/light/toggle");
  if (rq.op == Op::Get) url += l.entity;
  if (!http.begin(client, url)) {
    report(rq.index, ha::LightState::Unknown, ha::Result::NetworkError, toggle);
    return;
  }
  http.setReuse(true);  // keep-alive: the 4 refresh GETs share one connection
  http.setTimeout(HA_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HA_HTTP_TIMEOUT_MS);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code;
  if (rq.op == Op::Get) {
    code = http.GET();
  } else {
    char body[96];
    const size_t n = ha::buildToggleBody(body, sizeof body, l.entity);
    code = n ? http.POST(reinterpret_cast<uint8_t *>(body), n) : -1;
  }
  const ha::Result res = ha::mapHttpStatus(code);
  ha::LightState state = ha::LightState::Unknown;
  bool found = false;
  if (res == ha::Result::Ok) {
    const String payload = http.getString();
    if (rq.op == Op::Get) {
      state = ha::parseState(payload.c_str(), payload.length());
      found = true;
    } else {
      found = ha::findInStateArray(payload.c_str(), payload.length(), l.entity, state);
    }
  }
  http.end();
  ESP_LOGD(TAG, "%s %s -> %d", rq.op == Op::Get ? "GET" : "TOGGLE", l.entity, code);

  // The toggle response normally carries the new state; if not, ask.
  if (res == ha::Result::Ok && !found) {
    run(http, client, {Op::Get, rq.index}, toggle);
    return;
  }
  report(rq.index, state, res, toggle);
}

static void task(void *) {
  WiFiClient client;
  HTTPClient http;
  Request rq;
  for (;;) {
    if (xQueueReceive(queue, &rq, portMAX_DELAY) != pdTRUE) continue;
    run(http, client, rq, rq.op == Op::Toggle);
    pending--;
  }
}

void begin() {
  queue = xQueueCreate(8, sizeof(Request));
  xTaskCreatePinnedToCore(task, "ha", 8192, nullptr, 3, nullptr, 0);
}

static bool enqueue(const Request &rq) {
  pending++;
  if (xQueueSend(queue, &rq, 0) == pdTRUE) return true;
  pending--;
  return false;
}

bool refreshAll() {
  if (!queue || uxQueueSpacesAvailable(queue) < kLights) return false;
  for (int i = 0; i < kLights; i++) enqueue({Op::Get, uint8_t(i)});
  return true;
}

bool toggle(int i) { return queue && enqueue({Op::Toggle, uint8_t(i)}); }

bool busy() { return pending > 0; }

}  // namespace apps::ha_client
