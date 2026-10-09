# Libraries & tools

> **Status: plan.** Versions are pinned when each one is first used (see
> ROADMAP). Check this table before adding a dependency, and add a row when
> you do.

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
| UI toolkit | **LVGL** | 9.6.0 | MIT | `lib_deps` | widgets, scrolling with snap, animations, `lv_animimg` for sprites, canvas, image decoders, LittleFS driver; renders straight into `RGB565_SWAPPED` |
| Display transport | **esp_lcd** (ESP-IDF, in the Arduino core) | core | Apache-2.0 | built in | SPI panel IO in `quad_mode`, **asynchronous DMA**: LVGL renders the next chunk while the previous one is sent |
| AMOLED panel driver | **esp_lcd_sh8601** (Waveshare/Espressif) | 1.x (from Waveshare-ESP32-components) | Apache-2.0 | vendored → `lib/esp_lcd_amoled/` | SH8601 and CO5300 share the QSPI protocol (opcodes `0x02` cmd, `0x32` pixels); the CO5300 init sequence comes from the vendor BSP. One driver for V1 and V2. |
| Touch, IMU, RTC | **SensorLib** (lewisxhe) | 0.5.0 | MIT | `lib_deps` | `TouchDrvCSTXXX` (CST816 family incl. CST820) / FT3168, `SensorQMI8658`, `SensorPCF85063`. If CST820 support misbehaves: own ~50-line driver (regs `0x02`–`0x06`). |
| PMU | **XPowersLib** (lewisxhe) | 0.3.3 | MIT | `lib_deps` | AXP2101: VBUS, charging, battery %, PEK IRQs, `shutdown()` |
| IO expander | own driver in `core/board` | — | — | code | TCA9554 is 4 registers; avoids pulling in Adafruit XCA9554 + BusIO |
| Audio codec | **es8311** (Espressif) | from vendor example `15_ES8311` | Apache-2.0 | vendored → `lib/es8311/` | register setup; audio data via the core's `ESP_I2S` |
| JSON | **ArduinoJson** | 7.4.3 | MIT | `lib_deps` | Home Assistant responses |
| HTTP | `HTTPClient` | core | LGPL-2.1 (core) | built in | HA REST |
| Wi-Fi, NVS, FS, I2S, time | `WiFi`, `Preferences`, `LittleFS`, `ESP_I2S`, SNTP | core | core | built in | |
| Unit tests | Unity (PlatformIO test runner) | bundled | MIT | `[env:native]` | pure-logic modules only |

### Considered and not used

| Option | Why not |
| --- | --- |
| GFX Library for Arduino (what the vendor demos use) | its bitmap draw blocks until the transfer ends, so LVGL can't overlap rendering and DMA, and its drawing API is redundant next to LVGL. **Fallback** if the vendored esp_lcd driver gives trouble in M1. |
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
| `tools/assets.py` (ours) | — | — | Python 3.11+ (stdlib `tomllib`) | reads `assets/assets.toml`, runs both converters, slices sprite sheets, writes `src/ui/generated/` and `data/` |

## Fonts & art

| Asset | Licence | Use |
| --- | --- | --- |
| **Inter** | SIL OFL 1.1 | UI text |
| **Roboto Mono** | Apache-2.0 | big timer digits (monospaced, so they don't jitter while counting) |
| **Material Symbols Rounded** | Apache-2.0 | icon font: app icons, status bar, buttons |

The variable Material Symbols font is instanced to a static one (fill 1,
weight 500) with fontTools `varLib.instancer` in `tools/assets.py` if
lv_font_conv can't use the variable file directly; checked in M3.
