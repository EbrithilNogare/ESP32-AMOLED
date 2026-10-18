# Libraries & tools

> **Status: in use** (branch `feature/launcher-home-assistant`). Check this
> table before adding a dependency, and add a row when you do.

Rules:

- One library per job. Prefer what's already in the Arduino core / ESP-IDF.
- Pin exact versions in `platformio.ini` (`owner/name@x.y.z`, or a git URL
  with a tag).
- A small, stable driver (≤ a few hundred lines, permissive licence) can be
  **vendored** into `lib/<name>/`, keeping its licence header, when that's
  simpler than a dependency. List it here with its origin.
- Every third-party licence is listed here. Fonts and art go under
  `assets/` together with their licence files.

## Firmware

| Purpose | Library | Version | Licence | How | Notes |
| --- | --- | --- | --- | --- | --- |
| UI toolkit | **LVGL** | 9.6.0 | MIT | `lib_deps` (git tag) | widgets, scrolling with snap, animations; renders straight into `RGB565_SWAPPED`; its heap lives in PSRAM |
| Display transport | **esp_lcd** (ESP-IDF, in the Arduino core) | core | Apache-2.0 | built in | SPI panel IO in `quad_mode`, **asynchronous DMA**: LVGL renders the next chunk while the previous one is sent |
| AMOLED panel commands | derived from **esp_lcd_sh8601** (Waveshare/Espressif) | — | Apache-2.0 | ported → `src/core/display.cpp` | only the QSPI framing (opcodes `0x02` cmd, `0x32` pixels) and the vendor BSP init sequence were needed (~60 lines), so no separate library. One sequence for V1 and V2. |
| Touch | own driver | — | — | `src/core/touch.cpp` | CST820 (V2) and FT3168 (V1) share the register layout we need: 7 bytes from `0x00` |
| RTC | own driver | — | — | `src/core/rtc_clock.cpp` | PCF85063: one 7-byte BCD read at boot, one write after SNTP |
| IMU | own driver | — | — | `src/core/imu.cpp` | QMI8658 accelerometer: 5 register writes and one 6-byte read; values checked against SensorLib (MIT) |
| PMU | **XPowersLib** (lewisxhe) | 0.3.3 | MIT | `lib_deps` (git tag) | AXP2101: VBUS, charging, battery %, PEK IRQs; I2C through our locked bus via its callback API |
| IO expander | own driver | — | — | `src/core/board_init.cpp` | TCA9554 is 4 registers; avoids pulling in Adafruit XCA9554 + BusIO |
| Audio codec | register setup from **es8311** (Espressif) | from vendor example `15_ES8311` | Apache-2.0 | ported → `src/core/sound.cpp` | the original uses the legacy I2C driver, which can't coexist with `Wire`; audio data via the core's `ESP_I2S` |
| JSON | **ArduinoJson** | 7.4.3 | MIT | `lib_deps` (git tag) | Home Assistant responses (`lib/HaParse`) |
| HTTP | `HTTPClient` | core | LGPL-2.1 (core) | built in | HA REST, plain http with keep-alive |
| Wi-Fi, NVS, I2S, time | `WiFi`, `Preferences`, `ESP_I2S`, SNTP | core | core | built in | |
| OTA | `ArduinoOTA` + `ESPmDNS` + `Update` | core | core | built in | push updates via espota; rollback through the core's `verifyRollbackLater()` hook |
| Unit tests | Unity (PlatformIO test runner) | 2.6.x | MIT | `[env:native]` | pure-logic modules in `lib/` |
| UI simulator | LVGL on the host + `tools/sim/` | — | — | `[env:sim]` | runs the real apps against stubbed hardware, scripted flow test + screenshots |

Not used (yet): **SensorLib** (planned for touch/RTC/IMU; replaced by the
small own drivers above; revisit if the IMU needs its FIFO, gestures or
step counter), LittleFS
and `lv_animimg` sprites (no app needs files or sprite sheets so far).

### Considered and not used

| Option | Why not |
| --- | --- |
| GFX Library for Arduino (what the vendor demos use) | its bitmap draw blocks until the transfer ends, so LVGL can't overlap rendering and DMA, and its drawing API is redundant next to LVGL. **Fallback** if the esp_lcd panel code in `src/core/display.cpp` gives trouble in M1. |
| LovyanGFX, TFT_eSPI | no maintained support for these QSPI AMOLED controllers that we can rely on; drawing APIs redundant next to LVGL |
| Adafruit XCA9554 (+ BusIO) | a dependency chain for 4 registers |
| ESP-IDF BSP `waveshare/esp32_s3_touch_amoled_1_8` | needs the ESP-IDF component manager, not the Arduino/PlatformIO build; we reuse its init sequence and pin data instead |
| SquareLine Studio / EEZ Studio | generated UI code is hard to review and diff; layouts are small enough to write by hand. Fine for mockups. |
| Lottie (ThorVG), GIF | heavy CPU/RAM for this panel size; sprite sheets and LVGL animations cover our needs |

## Asset tools (developer machine and CI, not on the device)

| Tool | Version | Licence | Runtime | Used for |
| --- | --- | --- | --- | --- |
| **lv_font_conv** | 1.5.3 | MIT | Node ≥ 18 (`tools/package.json`, run with `npx`) | TTF/OTF → LVGL C font, subset of glyphs |
| **LVGLImage.py** (from LVGL `scripts/`, same tag as the firmware LVGL) | 9.6.0 | MIT | Python 3 + `pypng`, `lz4` (`tools/requirements.txt`) | PNG → LVGL C array or `.bin` |
| `tools/assets.py` (ours) | — | — | Python 3.11+ (stdlib `tomllib`), `fonttools` | reads `assets/assets.toml`, instances the icon font, runs both converters, writes `src/ui/generated/` (`--check` in CI) |
| `tools/sim/shots.py` (ours) | — | — | Python 3 + Pillow | simulator frames → `docs/design/screens/` and a contact sheet |
| `tools/gen_brush.py` (ours) | — | — | Python 3 + Pillow | draws the 8-ball's paint swipe mask |
| `tools/power_model.py` (ours) | — | — | Python 3 | the estimate tables in `docs/POWER.md` |
| clang-format | any recent | Apache-2.0 | `.clang-format` (Google style, 120 columns) | formatting; generated files are excluded |

## Fonts & art

| Asset | Licence | Use |
| --- | --- | --- |
| **Inter** | SIL OFL 1.1 | UI text |
| **Roboto Mono** | SIL OFL 1.1 | big timer digits (monospaced, so they don't jitter while counting) |
| **Material Symbols Rounded** | Apache-2.0 | icon font: app icons, status bar, buttons |
| **Permanent Marker** | Apache-2.0 | Magic 8-ball lettering (brush capitals). Rejected after rendering them side by side: Rubik Dirt / Distressed (too busy at 30 px), Frijole, Black Ops One (stencil), Bangers (comic), Rock Salt (thin), Sedgwick Ave Display, Caveat Brush, Rubik Wet Paint |
| paint swipe (`assets/images/brush.png`) | ours (`tools/gen_brush.py`) | the 8-ball's red swipe, an A8 mask |

The variable Material Symbols font is instanced to static ones (fill 1,
weight 500, optical size per pixel size) with fontTools `varLib.instancer`
in `tools/assets.py`; lv_font_conv can't use the variable file directly.
