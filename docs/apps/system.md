# System

## Purpose

Device information and maintenance: firmware version, battery, Wi-Fi,
memory, and the **wireless firmware update** (OTA, ARCHITECTURE §12).

## Screens

```
 System (scrolling list)             Wireless update
┌──────────────────────────┐        ┌──────────────────────────┐
│ System                   │        │ Wireless update          │
│                          │        │                          │
│ Firmware   v0.3.0-5-gabc │        │        ╭──────╮          │
│ Board      V2 · S3R8     │  tap   │       │  42 % │          │  progress ring
│ Battery    82 % · 3.95 V │ ─────► │        ╰──────╯          │
│ Charging   yes (USB)     │        │                          │
│ Wi-Fi      -61 dBm       │        │ esp32-amoled.local       │
│ IP         192.168.1.57  │        │ 192.168.1.57             │
│ Free RAM   212 KB / 7.6 MB│       │ Don't power off          │
│ Uptime     0:03:12       │        │                          │
│                          │        │                          │
│ [ Wireless update ]      │        │                          │
└──────────────────────────┘        └──────────────────────────┘
```

Update screen states: *Connecting Wi-Fi* → *Waiting for upload* (host, IP,
version) → *Receiving n %* → *Done, restarting…* or *Failed: reason*
(auth, size, wrong image, battery low).

## Interaction

| Input | Effect |
| --- | --- |
| Tap "Wireless update" | open the update screen, start the listener |
| Back / Home (waiting) | stop the listener, leave |
| Back / Home (receiving) | ignored, so a transfer can't be interrupted by accident |

## Power

Info list: `Normal`. Update screen: `KeepAwake` while open, Wi-Fi on.

## Persistence

None (after a successful update the device restarts into the new firmware,
which confirms itself after the health check).

## Config

`OTA_HOSTNAME`, `OTA_MIN_BATTERY_PCT`,
`OTA_HEALTHY_AFTER_MS` in `config.h`; `OTA_PASSWORD` in `secrets.h` (empty
disables OTA).

## Acceptance (on device)

- [ ] Info values plausible (battery vs. multimeter, RSSI, version matches the build)
- [ ] `pio run -e amoled-ota -t upload` updates the device; it restarts into the new version
- [ ] Wrong password is rejected and shown on screen
- [ ] Pulling power mid-transfer → device still boots the old firmware
- [ ] A deliberately crashing test build rolls back to the previous firmware
- [ ] Listener refuses below 30 % battery when not charging

## Later

Pull updates from GitHub Releases ("Check for updates"); settings entries
(brightness, timeouts) until a dedicated Settings app exists.
