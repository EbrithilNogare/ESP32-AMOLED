# Home Assistant lights

## Purpose

Toggle the Home Assistant lights on the local network. The lights aren't
configured on the device: every `light.*` entity is loaded from Home
Assistant, sorted by name.

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

Up to four lights fill the 2×2 grid above; with more, the tiles are 148 px
tall (smaller icon, one-line name with "…") in a scrolling two-column grid
(`46-lights-many.png`). At most `HA_MAX_LIGHTS` (16), the first by name.
Before the first list: "Loading lights…"; an empty list: "No lights in Home
Assistant".

A chip at the bottom explains a problem: "Connecting…" (no Wi-Fi), "Home
Assistant not found" (the host name doesn't resolve), "Can't reach Home
Assistant" (no answer, wrong port, server error), "HA token rejected —
check secrets.h" (`47-lights-not-found.png`).

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
| Load the lights | `GET {HA_BASE_URL}/api/states` → every entity; the `light.*` ones are kept, with `friendly_name` and state |
| Read a light | `GET {HA_BASE_URL}/api/states/<entity_id>` → `{"state":"on",…}` |
| Toggle | `POST {HA_BASE_URL}/api/services/light/toggle` body `{"entity_id":"<entity_id>"}` |

Headers: `Authorization: Bearer {HA_TOKEN}`, `Content-Type: application/json`.

- `ha_client` runs in its own FreeRTOS task: requests go in a queue, results
  come back as `Ev::HaResult` events; the UI never blocks on the network.
- On resume (and when Wi-Fi comes up): load the light list, which carries
  every state too. While visible: poll the states every `HA_POLL_MS` (one
  GET per light over a kept-alive connection) and reload the list every
  `HA_LIST_MS` for new or renamed lights.
- The list body can be large (every entity of the house), so it is read with
  HTTP/1.0 (no chunked encoding) and parsed straight off the socket one
  entity at a time (`ha::parseLightList`); only the lights are kept.
- Host: `HA_BASE_URL` may be an IP, a `.local` name, or a bare name; a bare
  name the router's DNS doesn't know is tried as `<name>.local` (lwIP's mDNS
  lookup). The address is kept until a request fails.
- While Home Assistant can't be reached, polling backs off, doubling up to
  `HA_RETRY_MAX_MS`.
- Results carry the list generation they were asked for; results for a list
  that has since been replaced are dropped.
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

The light list (ids and names) in NVS (`halights`), written only when it
changes, so the tiles are there at once after a reboot. The last known state
of each light in `RtcState`. When the app is opened after a wake they're
shown immediately, marked *stale*, until the first refresh. Target: fresh states
≤ 1.5 s after wake, using a fast Wi-Fi reconnect: the network's remembered
channel and BSSID plus its remembered DHCP lease (or `WIFI_STATIC_IP`).

## Config

`config.h`:

```cpp
#define HA_MAX_LIGHTS 16          // the first ones by name are shown
#define HA_POLL_MS 3000           // light states while the app is open
#define HA_LIST_MS 60000          // the light list again (new or renamed lights)
#define HA_RETRY_MAX_MS 30000     // polling backs off up to this while HA can't be reached
#define HA_HTTP_TIMEOUT_MS 1500
```

`secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD`, `HA_BASE_URL`
(e.g. `http://192.168.1.10:8123` or `http://homeassistant.local:8123`), `HA_TOKEN`, optional `WIFI_STATIC_IP`,
`WIFI_GATEWAY`, `WIFI_SUBNET`, `WIFI_DNS`.

## Errors

| Situation | Shown |
| --- | --- |
| No Wi-Fi | banner "Connecting…", tiles stale, retry in background |
| Host name doesn't resolve | banner "Home Assistant not found", retry with back-off |
| HA unreachable / timeout / 5xx | banner "Can't reach Home Assistant", tiles stale, retry with back-off |
| HTTP 401 | banner "HA token rejected — check secrets.h" (until a request succeeds; network errors keep it) |
| 404 / `unavailable` | that tile "n/a" |

## Security

The token has the rights of its HA user, so create a dedicated HA user for
the device. The token sits in `secrets.h` and in flash, so anyone holding the
device can extract it; revoke it in HA if the device is lost. Plain HTTP is
fine on a trusted LAN; HTTPS can come later.

## Acceptance (on device)

- [ ] The tiles are the HA lights, by name, without configuring any
- [ ] `HA_BASE_URL` with a bare host name finds HA through `.local`
- [ ] A light added or renamed in HA shows up within `HA_LIST_MS`
- [ ] Each tile toggles the right light; the HA web UI agrees
- [ ] A change made in HA shows up within one poll
- [ ] Wrong token → 401 banner, UI stays responsive
- [ ] HA server down → stale/n/a, no freeze
- [ ] Opened after deep sleep: tiles visible instantly (stale), fresh ≤ 1.5 s

## Later

Brightness/colour, choosing which lights (or an HA area / label), scenes, other entity types
(switches, covers), WebSocket push updates.
