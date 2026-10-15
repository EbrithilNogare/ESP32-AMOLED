# System

## Purpose

Device information and maintenance: firmware version, battery, Wi-Fi,
memory, the **wireless firmware update** (OTA, ARCHITECTURE §12) and **touch
calibration**.

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
│ Standby    54 days       │        │                          │
│ Wi-Fi      -61 dBm       │        │ esp32-amoled.local       │
│ IP         192.168.1.57  │        │ 192.168.1.57             │
│ Free RAM   212 KB / 7.6 MB│       │ Don't power off          │
│ Uptime     0:03:12       │        │                          │
│                          │        │                          │
│ [ Update ]  [ Touch ]    │        │                          │
└──────────────────────────┘        └──────────────────────────┘
```

**Standby** is how many days a full charge lasts in deep sleep, measured
across the sleeps since the last charge (ARCHITECTURE §8.6): "–" before the
first sleep on battery, "measuring · n h" until 2 % is gone or a day has
passed, "> n days" when under 2 % was lost (a lower bound).

Touch calibration (`44-touch-calibration.png`, `45-touch-check.png`): a
cross near each corner, then the centre (40 px in from the edges), one at a
time, "n / 5" under the hint. After the fifth, the correction is applied and
a dot follows the finger to check it, with **Redo** and **Save**.

Update screen states: *Connecting Wi-Fi* → *Waiting for upload* (host, IP,
version) → *Receiving n %* → *Done, restarting…* or *Failed: reason*
(auth, size, wrong image, battery low).

## Interaction

| Input | Effect |
| --- | --- |
| Tap "Update" | open the update screen, start the listener |
| Tap "Touch" | touch calibration |
| Tap anywhere (calibrating) | records the tap for the cross shown (a tap off the cross counts; it's the panel's error being measured) |
| Redo / Save (checking) | start over / keep the correction and go back |
| Back (calibrating or checking) | leave; the saved correction stays in use |
| Back / Home (waiting) | stop the listener, leave |
| Back / Home (receiving) | ignored, so a transfer can't be interrupted by accident |

## Power

Info list: `Normal`. Update screen: `KeepAwake` while open, Wi-Fi on.

## Touch calibration

The crosses are read with the correction off (`core::touch::setRaw`). Per
axis, `lib/TouchCal` fits `screen = raw × scale + offset` by least squares.
The fit is refused, and the five crosses start over, if the scale is off by
more than 20 %, the offset by more than 64 px, or any tap misses its cross by
more than 24 px after the fit (a sloppy tap). `core::touch` applies the
correction to every touch; it's saved in NVS (`touchcal`) and loaded at boot.

## Persistence

Touch calibration (`touchcal`, see above). Otherwise none (after a successful update the device restarts into the new firmware,
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
- [ ] After calibrating, the dot sits under the finger near every edge, and
      the Dice die buttons and stepper react where their icons are
- [ ] The calibration survives a reboot and a firmware update
- [ ] A deliberately crashing test build rolls back to the previous firmware
- [ ] Listener refuses below 30 % battery when not charging

## Later

Pull updates from GitHub Releases ("Check for updates"); settings entries
(brightness, timeouts) until a dedicated Settings app exists.
