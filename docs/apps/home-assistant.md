# Home Assistant lights

## Purpose

Toggle four preconfigured Home Assistant lights on the local network from a
2×2 grid.

## Screens

```
┌────────────┬────────────┐
│    💡      │    💡      │   four 184 × 224 tiles (minus 4 px safe margin)
│  Living    │  Kitchen   │
│   room     │            │   ON  : warm yellow fill, dark text
│    ON      │    OFF     │   OFF : dark grey fill, light text
├────────────┼────────────┤   pending : current look + spinner
│    💡      │    💡      │   n/a : outline only, dimmed, "n/a"
│  Bedroom   │  Hallway   │   stale : normal look + small "…" marker
│    OFF     │    n/a     │
└────────────┴────────────┘
```

A thin banner over the grid shows "Connecting…", "No Wi-Fi", or
"HA token rejected" when relevant.

## Interaction

| Input | Effect |
| --- | --- |
| Tap tile | toggle that light (optimistic *pending*, then the confirmed state) |
| Back / Home | back to the menu |
| (later) long-press tile | brightness slider |

## Data flow

Home Assistant REST API, plain HTTP on the LAN, long-lived access token
(HA → Profile → Security → Long-lived access tokens):

| Action | Request |
| --- | --- |
| Read a light | `GET {HA_BASE_URL}/api/states/<entity_id>` → `{"state":"on",…}` |
| Toggle | `POST {HA_BASE_URL}/api/services/light/toggle` body `{"entity_id":"<entity_id>"}` |

Headers: `Authorization: Bearer {HA_TOKEN}`, `Content-Type: application/json`.

- `ha_client` runs in its own FreeRTOS task: requests go in a queue, results
  come back as `Ev::HaResult` events; the UI never blocks on the network.
- On resume (and when Wi-Fi comes up): fetch all 4 states. While visible:
  poll every `HA_POLL_MS`.
- Toggle → the POST returns the changed states; if the entity is missing from
  the response, follow up with a GET. On failure the tile keeps its old state
  and shakes. While a toggle is pending, poll results for that light are
  ignored (they were queued before the tap); only the toggle's own result
  ends the spinner.
- Wi-Fi is requested from `Net` on resume and released on pause (on battery
  it is switched off when no app needs it).

Later: the WebSocket API (`/api/websocket`, `subscribe_entities`) for
instant updates when the device is on the charger and stays awake.

## Power

`Normal`. On battery: the default dim → black → deep-sleep timeline.

## Persistence

Last known state of each light in `RtcState`. When the app is opened after a
wake they're shown immediately, marked *stale*, until the first refresh. Target: fresh states
≤ 1.5 s after wake, using a fast Wi-Fi reconnect with the cached channel and
BSSID plus an optional static IP.

## Config

`config.h`:

```cpp
// ---- home assistant ----
#define HA_LIGHTS { \
    {"light.living_room", "Living room"}, \
    {"light.kitchen",     "Kitchen"},     \
    {"light.bedroom",     "Bedroom"},     \
    {"light.hallway",     "Hallway"},     \
}                                   // exactly 4, order: TL, TR, BL, BR
#define HA_POLL_MS          3000
#define HA_HTTP_TIMEOUT_MS  1500
```

`secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD`, `HA_BASE_URL`
(e.g. `http://192.168.1.10:8123`), `HA_TOKEN`, optional `WIFI_STATIC_IP`,
`WIFI_GATEWAY`, `WIFI_SUBNET`, `WIFI_DNS`.

## Errors

| Situation | Shown |
| --- | --- |
| No Wi-Fi | banner "Connecting…", tiles stale, retry in background |
| HA unreachable / timeout | tiles stale, retry on next poll |
| HTTP 401 | banner "HA token rejected — check secrets.h" (until a request succeeds; network errors keep it) |
| 404 / `unavailable` | that tile "n/a" |

## Security

The token has the rights of its HA user, so create a dedicated HA user for
the device. The token sits in `secrets.h` and in flash, so anyone holding the
device can extract it; revoke it in HA if the device is lost. Plain HTTP is
fine on a trusted LAN; HTTPS can come later.

## Acceptance (on device)

- [ ] Each tile toggles the right light; the HA web UI agrees
- [ ] A change made in HA shows up within one poll
- [ ] Wrong token → 401 banner, UI stays responsive
- [ ] HA server down → stale/n/a, no freeze
- [ ] Opened after deep sleep: tiles visible instantly (stale), fresh ≤ 1.5 s

## Later

Brightness/colour, more lights via pages, scenes, other entity types
(switches, covers), WebSocket push updates.
