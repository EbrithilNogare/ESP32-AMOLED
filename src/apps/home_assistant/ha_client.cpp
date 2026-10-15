#include "ha_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>

#include "core/events.h"
#include "core/net.h"
#include "secrets_or_example.h"

namespace apps::ha_client {

[[maybe_unused]] static const char *TAG = "ha";

enum class Op : uint8_t { List, Get, Toggle };
struct Request {
  Op op;
  uint8_t index, gen;
  char entity[ha::kEntityMax];  // a copy: the task never reads the UI's list
};

static QueueHandle_t queue;
static std::atomic<int> pending{0};  // queued or running; counted up before the send
static std::atomic<bool> listInFlight{false};  // until the UI took the result: `loaded` is the task's

// The list, owned by the UI task. The task loads into `loaded` and posts
// Ev::HaLights; adopt() copies it over once the UI has the event.
static ha::LightInfo lights[kMaxLights], loaded[kMaxLights];
static int nLights, nLoaded;
static uint8_t gen;

int count() { return nLights; }
const ha::LightInfo &light(int i) { return lights[i < 0 ? 0 : i >= nLights ? nLights - 1 : i]; }
uint8_t generation() { return gen; }

static bool sameList(const ha::LightInfo *a, int na, const ha::LightInfo *b, int nb) {
  if (na != nb) return false;
  for (int i = 0; i < na; i++)
    if (strcmp(a[i].entity, b[i].entity) != 0 || strcmp(a[i].name, b[i].name) != 0) return false;
  return true;
}

void setList(const ha::LightInfo *l, int n) {
  nLights = n < 0 ? 0 : n > kMaxLights ? kMaxLights : n;
  memcpy(lights, l, sizeof(ha::LightInfo) * size_t(nLights));
  gen++;
}

bool adopt(ha::Result r) {
  listInFlight = false;
  if (r != ha::Result::Ok) return false;
  const bool changed = !sameList(lights, nLights, loaded, nLoaded);
  memcpy(lights, loaded, sizeof(ha::LightInfo) * size_t(nLoaded));  // states are fresh either way
  nLights = nLoaded;
  if (changed) gen++;
  return changed;
}

// ------------------------------------------------------------ the server

// HA_BASE_URL is "http://host[:port]". A bare host name ("homeassistant")
// isn't in most routers' DNS, so after it fails "<host>.local" (mDNS) is
// tried. The address is kept until a request fails, then looked up again.
static String host;
static uint16_t port = 8123;
static IPAddress address;
static bool resolved;

static void parseBaseUrl() {
  String u = HA_BASE_URL;
  if (u.startsWith("http://")) u.remove(0, 7);
  const int slash = u.indexOf('/');
  if (slash >= 0) u.remove(slash);
  const int colon = u.indexOf(':');
  if (colon >= 0) {
    port = uint16_t(u.substring(colon + 1).toInt());
    u.remove(colon);
  }
  host = u;
}

static bool resolve() {
  if (resolved) return true;
  if (address.fromString(host) || WiFi.hostByName(host.c_str(), address) == 1) {
    resolved = true;
  } else if (host.indexOf('.') < 0 && WiFi.hostByName((host + ".local").c_str(), address) == 1) {
    ESP_LOGI(TAG, "%s not in DNS; found %s.local", host.c_str(), host.c_str());
    resolved = true;
  }
  if (resolved) ESP_LOGI(TAG, "Home Assistant at %s:%u", address.toString().c_str(), port);
  return resolved;
}

static String url(const char *path) { return "http://" + address.toString() + ":" + String(port) + path; }

// ------------------------------------------------------------ requests

// `toggle`: the result answers a tap (also when a GET had to follow the POST).
static void report(const Request &rq, ha::LightState s, ha::Result r, bool toggle) {
  core::events::post({core::Ev::HaResult, rq.index, pack(s, r, toggle, rq.gen)});
}

static void setup(HTTPClient &http) {
  http.setTimeout(HA_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HA_HTTP_TIMEOUT_MS);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
}

// What deserializeJson needs, reading with the stream's timeout (Stream::read()
// returns -1 at once when the next bytes haven't arrived yet).
struct TimedReader {
  Stream &s;
  int read() {
    char c;
    return s.readBytes(&c, 1) == 1 ? static_cast<unsigned char>(c) : -1;
  }
  size_t readBytes(char *buf, size_t n) { return s.readBytes(buf, n); }
};

static ha::Result loadList(HTTPClient &http, WiFiClient &client) {
  // HTTP/1.0: no chunked encoding, so the body can be parsed straight off the
  // socket, one entity at a time, however big the state list is.
  http.useHTTP10(true);
  if (!http.begin(client, url("/api/states"))) return ha::Result::NetworkError;
  setup(http);
  const int code = http.GET();
  ha::Result res = ha::mapHttpStatus(code);
  if (res == ha::Result::Ok) {
    client.setTimeout(HA_HTTP_TIMEOUT_MS);
    TimedReader r{http.getStream()};
    nLoaded = ha::parseLightList(r, loaded, kMaxLights);
    if (nLoaded < 0) {
      nLoaded = 0;
      res = ha::Result::BadResponse;
    }
  }
  http.end();
  http.useHTTP10(false);
  ESP_LOGI(TAG, "light list: %d, %s", res == ha::Result::Ok ? nLoaded : -1, ha::resultName(res));
  return res;
}

static void run(HTTPClient &http, WiFiClient &client, const Request &rq, bool toggle) {
  String path = rq.op == Op::Get ? String("/api/states/") + rq.entity : String("/api/services/light/toggle");
  if (!http.begin(client, url(path.c_str()))) {
    report(rq, ha::LightState::Unknown, ha::Result::NetworkError, toggle);
    return;
  }
  http.setReuse(true);  // keep-alive: the refresh GETs share one connection
  setup(http);

  int code;
  if (rq.op == Op::Get) {
    code = http.GET();
  } else {
    char body[96];
    const size_t n = ha::buildToggleBody(body, sizeof body, rq.entity);
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
      found = ha::findInStateArray(payload.c_str(), payload.length(), rq.entity, state);
    }
  }
  http.end();
  if (res == ha::Result::NetworkError) resolved = false;  // look the address up again next time
  ESP_LOGD(TAG, "%s %s -> %d", rq.op == Op::Get ? "GET" : "TOGGLE", rq.entity, code);

  // The toggle response normally carries the new state; if not, ask.
  if (res == ha::Result::Ok && !found) {
    Request get = rq;
    get.op = Op::Get;
    run(http, client, get, toggle);
    return;
  }
  report(rq, state, res, toggle);
}

static void task(void *) {
  WiFiClient client;
  HTTPClient http;
  Request rq;
  parseBaseUrl();
  for (;;) {
    if (xQueueReceive(queue, &rq, portMAX_DELAY) != pdTRUE) continue;
    const bool toggle = rq.op == Op::Toggle;
    if (!core::net::connected()) {
      if (rq.op == Op::List)
        core::events::post({core::Ev::HaLights, 0, uint32_t(ha::Result::NetworkError)});
      else
        report(rq, ha::LightState::Unknown, ha::Result::NetworkError, toggle);
    } else if (!resolve()) {
      if (rq.op == Op::List)
        core::events::post({core::Ev::HaLights, 0, uint32_t(ha::Result::HostNotFound)});
      else
        report(rq, ha::LightState::Unknown, ha::Result::HostNotFound, toggle);
    } else if (rq.op == Op::List) {
      const ha::Result r = loadList(http, client);
      if (r == ha::Result::NetworkError) resolved = false;
      core::events::post({core::Ev::HaLights, 0, uint32_t(r)});
    } else {
      run(http, client, rq, toggle);
    }
    pending--;
  }
}

void begin() {
  queue = xQueueCreate(kMaxLights + 4, sizeof(Request));
  xTaskCreatePinnedToCore(task, "ha", 8192, nullptr, 3, nullptr, 0);
}

static bool enqueue(const Request &rq) {
  pending++;
  if (xQueueSend(queue, &rq, 0) == pdTRUE) return true;
  pending--;
  return false;
}

static Request request(Op op, int i) {
  Request rq{op, uint8_t(i), gen, {}};
  if (op != Op::List) strlcpy(rq.entity, lights[i].entity, sizeof rq.entity);
  return rq;
}

bool loadLights() {
  if (!queue || listInFlight) return false;
  listInFlight = true;
  if (enqueue(request(Op::List, 0))) return true;
  listInFlight = false;
  return false;
}

bool refresh() {
  if (!queue || int(uxQueueSpacesAvailable(queue)) < nLights) return false;
  for (int i = 0; i < nLights; i++) enqueue(request(Op::Get, i));
  return true;
}

bool toggle(int i) { return queue && i >= 0 && i < nLights && enqueue(request(Op::Toggle, i)); }

bool busy() { return pending > 0; }

}  // namespace apps::ha_client
