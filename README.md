# ESP32-AMOLED

[![Build](https://github.com/EbrithilNogare/ESP32-AMOLED/actions/workflows/build.yml/badge.svg)](https://github.com/EbrithilNogare/ESP32-AMOLED/actions/workflows/build.yml)

Firmware skeleton for the
[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm?sku=29957):
ESP32-S3R8 (16 MB flash, 8 MB PSRAM), 1.8" 368×448 AMOLED with touch, AXP2101
PMU, QMI8658 IMU, PCF85063 RTC, ES8311 audio, microSD.

Right now the app only prints a boot banner over USB and idles — the structure,
config, and hardware data are ready for a real app.

**Full board spec, pinout, I2C map, and library notes: [docs/HARDWARE.md](docs/HARDWARE.md).**

## Layout

```
ESP32-AMOLED/
├── include/
│   ├── board.h              hardware pins & I2C addresses (fixed by the PCB)
│   ├── config.h             app tunables (committed)
│   └── secrets.example.h    template for secrets.h (Wi-Fi, OTA) — secrets.h is gitignored
├── src/main.cpp             entry point (setup/loop)
├── lib/                     project-private libraries, one folder each
├── data/                    files packed into the LittleFS partition
├── docs/
│   ├── HARDWARE.md          everything about the board
│   ├── DECISIONS.md         why things are the way they are
│   └── LESSONS.md           gotchas learned along the way
├── AGENTS.md                instructions for AI coding agents (CLAUDE.md imports it)
├── partitions.csv           16 MB: 2 × 4 MB OTA apps, ~8 MB LittleFS, core dump
├── platformio.ini           build environments
├── .vscode/tasks.json       build / flash / monitor tasks
└── .github/workflows/       CI: checks that both environments build
```

## Setup

1. Install VS Code with the **pioarduino IDE** extension (recommended
   automatically when you open the folder), or PlatformIO Core:
   `pip install platformio`.
2. Create your secrets file:
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
   Without it the build falls back to the example values (that is how CI builds).
3. Check the label on the back of the board: V1 (SH8601/FT3168) or V2
   (CO5300/CST820). Set `BOARD_REVISION` in `include/board.h` (default `2`).

## Build & flash

In VS Code: **Terminal → Run Task…** (or `Ctrl/Cmd+Shift+B` for the default
build) and pick the environment when asked:

| Task | Does |
| --- | --- |
| ESP: build | compile |
| ESP: build & upload & monitor | compile, flash over USB-C, open serial |
| ESP: upload | compile + flash |
| ESP: monitor | serial monitor (115200) |
| ESP: upload filesystem (data/) | flash the LittleFS image built from `data/` |
| ESP: clean | delete build output |
| ESP: erase flash | wipe the whole chip (NVS, filesystem, app) |

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
| `amoled-debug` | debug build, core log level VERBOSE, `APP_DEBUG=1` |

## Configuration

- **`include/board.h`** — pins and addresses. Don't change unless the hardware does.
- **`include/config.h`** — app settings (serial baud, loop period, …). Committed.
- **`include/secrets.h`** — credentials. Never committed; keep
  `secrets.example.h` in sync when adding new keys.
- **`platformio.ini`** — platform version (pinned), board, flags, and
  commented-out `lib_deps` for every onboard peripheral at the versions the
  vendor examples use.

## CI

`.github/workflows/build.yml` builds `amoled` and `amoled-debug` on every push
and PR (with PlatformIO cached) and attaches `firmware.bin` /
`firmware.factory.bin` as an artifact.

## Contributing

Small changes go straight to `main`; large independent features get a
`feature/<name>` branch and a PR. Record decisions in
[docs/DECISIONS.md](docs/DECISIONS.md) and gotchas in
[docs/LESSONS.md](docs/LESSONS.md). Agent instructions: [AGENTS.md](AGENTS.md).
