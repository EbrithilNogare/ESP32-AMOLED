# ESP32-AMOLED

[![Build](https://github.com/EbrithilNogare/ESP32-AMOLED/actions/workflows/build.yml/badge.svg)](https://github.com/EbrithilNogare/ESP32-AMOLED/actions/workflows/build.yml)

Smartwatch-style launcher firmware for the
[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm?sku=29957):
ESP32-S3R8 (16 MB flash, 8 MB PSRAM), 1.8" 368×448 AMOLED with touch, AXP2101
PMU, QMI8658 IMU, PCF85063 RTC, ES8311 audio, microSD.

A scrolling menu of round app icons, with **Home Assistant lights** (2×2
switches), a **bot face**, **Pomodoro** (with a low-power minimal view),
an **espresso shot timer**, **Dice** (wireframe d4–d100, multiple dice, advantage), a **Level** (bubble vial flat, attitude horizon on an edge) and **System** (info + wireless update). BOOT is
Back and PWR is Home, a long press powers off into deep sleep, and BOOT wakes
it. Battery and charger power profiles dim and blank the screen when idle. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md)
and the simulator screenshots in
[docs/design/firmware-screens.png](docs/design/firmware-screens.png).

> **Status:** builds, host tests and the PC simulator pass; **not yet verified
> on the board**. Work through the ROADMAP's on-device checklists from M1.

**Full board spec, pinout, I2C map, and library notes: [docs/HARDWARE.md](docs/HARDWARE.md).**

## Layout

```
ESP32-AMOLED/
├── include/
│   ├── board.h              hardware pins & I2C addresses (fixed by the PCB)
│   ├── config.h             app tunables (committed)
│   ├── lv_conf.h            LVGL configuration
│   └── secrets.example.h    template for secrets.h (Wi-Fi, HA, OTA) — secrets.h is gitignored
├── src/
│   ├── main.cpp             wiring: setup() bring-up, loop() servicing
│   ├── core/                hardware + system services (display, touch, power, net, OTA, …)
│   ├── apps/                app framework + one folder per app
│   └── ui/                  theme, power animation, generated fonts/icons
├── lib/                     Arduino-free logic, unit-tested on the PC
├── test/                    Unity tests for lib/
├── assets/                  font sources + assets.toml (input of tools/assets.py)
├── tools/                   asset generator, version/size scripts, sim/ (PC simulator)
├── data/                    files packed into the LittleFS partition (unused so far)
├── docs/
│   ├── HARDWARE.md          everything about the board
│   ├── ARCHITECTURE.md      system design (launcher, power, Home Assistant)
│   ├── ROADMAP.md           milestones and on-device checklists
│   ├── apps/                one spec per app
│   ├── LIBRARIES.md         dependencies, versions, licences
│   ├── UI_DESIGN.md         design system + asset pipeline
│   ├── design/              UI mockup (ui-mockup.html) + simulator screenshots
│   ├── DECISIONS.md         why things are the way they are
│   └── LESSONS.md           gotchas learned along the way
├── AGENTS.md                instructions for AI coding agents (CLAUDE.md imports it)
├── partitions.csv           16 MB: 2 × 4 MB OTA apps, ~8 MB LittleFS, core dump
├── platformio.ini           build environments
├── .vscode/tasks.json       build / flash / monitor tasks
└── .github/workflows/       CI: firmware builds, host tests + simulator, asset check
```

## Setup

1. Install VS Code with the **pioarduino IDE** extension (recommended
   automatically when you open the folder), or PlatformIO Core:
   `pip install platformio`.
2. Create your secrets file:
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
   Fill in Wi-Fi, the Home Assistant URL + long-lived token, and an OTA
   password. The 4 light entities are `HA_LIGHTS` in `include/config.h`.
   Without `secrets.h` the build falls back to the example values through
   `include/secrets_or_example.h` (that is how CI builds); their Wi-Fi SSID
   is empty, so such a build never tries to connect.
3. The board revision, V1 (SH8601/FT3168) or V2 (CO5300/CST820), is probed
   at boot by its touch address (`src/core/board_init.cpp`).
   `BOARD_REVISION` in `include/board.h` (default `2`) is only the fallback
   when no touch controller answers.

## Build & flash

In VS Code: **Terminal → Run Task…** (or `Ctrl/Cmd+Shift+B` for the default
build) and pick the environment when asked:

| Task | Does |
| --- | --- |
| ESP: build | compile |
| ESP: build & upload & monitor | compile, flash over USB-C, open serial |
| ESP: upload | compile + flash |
| ESP: upload OTA (Wi-Fi) | flash over Wi-Fi; open System → Wireless update on the device first and set `ESP_OTA_PASSWORD` in your shell |
| ESP: monitor | serial monitor (115200) |
| ESP: upload filesystem (data/) | flash the LittleFS image built from `data/` |
| ESP: clean | delete build output |
| ESP: erase flash | wipe the whole chip (NVS, filesystem, app) |
| Test: unit tests (PC) | `pio test -e native` |
| Test: UI simulator + screenshots (PC) | run every app on the PC, check the flow, regenerate `docs/design/screens/` |
| Assets: regenerate fonts/images | `python tools/assets.py` after editing `assets/` |

Same from a terminal:

```bash
pio run                        # build (default env: amoled)
pio run -t upload -t monitor   # flash + serial
pio run -e amoled-debug -t upload
```

If the board doesn't show up as a serial port: hold **BOOT**, replug USB,
release BOOT, then flash.

### Environments

| Env | Purpose |
| --- | --- |
| `amoled` | release — core log level ERROR |
| `amoled-debug` | debug build, core log level VERBOSE, `APP_DEBUG=1` (LVGL warnings logged) |
| `amoled-ota` | release build uploaded over Wi-Fi (espota) |
| `native` | host unit tests for `lib/` |
| `sim` | PC simulator + UI flow test (`.pio/build/sim/program`) |

### PC tests

```bash
pio test -e native                                 # logic unit tests
pio run -e sim && .pio/build/sim/program           # UI flow test, writes frames to .pio/sim/
python tools/sim/shots.py                          # frames → docs/design/screens/ (needs Pillow)
python tools/assets.py --check                     # generated fonts match assets/ (needs tools/requirements.txt + npm ci)
```

## Configuration

- **`include/board.h`** — pins and addresses. Don't change unless the hardware does.
- **`include/config.h`** — app tunables: brightness levels, idle timeouts,
  button timings, HA entities and poll rate, pomodoro/espresso durations, …
  Committed.
- **`include/secrets.h`** — credentials. Never committed; keep
  `secrets.example.h` in sync when adding new keys.
- **`platformio.ini`** — platform version (pinned), board, flags, and pinned
  `lib_deps` (LVGL, XPowersLib, ArduinoJson; see docs/LIBRARIES.md).

## CI

`.github/workflows/build.yml` on every push and PR:

- builds `amoled` and `amoled-debug` (failing above the 3.5 MiB OTA budget)
  and attaches `firmware.bin` / `firmware.factory.bin`;
- runs the host unit tests and the simulator flow test, and attaches the
  simulator screenshots;
- checks that `src/ui/generated/` matches what `tools/assets.py` produces.

## Contributing

Small changes go straight to `main`; large independent features get a
`feature/<name>` branch and a PR. Record decisions in
[docs/DECISIONS.md](docs/DECISIONS.md) and gotchas in
[docs/LESSONS.md](docs/LESSONS.md). Agent instructions: [AGENTS.md](AGENTS.md).
