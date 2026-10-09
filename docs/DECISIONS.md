# Decisions

Why the project is set up the way it is. Newest first. Add an entry when you
make a choice someone might later question.

Format: `## YYYY-MM-DD — title`, then *Context*, *Decision*, *Consequences*.

## 2026-10-09 — OTA updates (plan)

**Context:** The owner wants wireless updates if the firmware stays within the
limits. The partition table already has two 4 MB OTA slots.
**Decision:** v1 is push OTA from the PC (ArduinoOTA/espota), started from a
System app screen that keeps the device awake. App rollback uses the core's
bootloader (rollback enabled) plus a health check before the new image is
marked valid. Firmware budget ≤ 3.0 MB target, 3.5 MB hard limit, checked
in CI. Version from `git describe`. Pulling from GitHub Releases comes later.
**Consequences:** Every milestone keeps the image inside the budget; big
assets go to LittleFS. The listener never runs without a password, and only
on demand (or on the charger if opted in).

## 2026-10-09 — Libraries, rendering and assets (plan)

**Context:** Picking the software stack before implementation. Details:
LIBRARIES.md, UI_DESIGN.md, ARCHITECTURE.md §5–6.
**Decision:**
- **LVGL 9.6** for all UI, rendering directly in `RGB565_SWAPPED`.
- Display through **esp_lcd** QSPI (async DMA) + a vendored Apache-2.0
  SH8601/CO5300 panel driver, instead of GFX Library for Arduino (whose
  blocking flush stops LVGL from overlapping rendering and DMA). GFX stays the
  fallback.
- SensorLib (touch, IMU, RTC), XPowersLib (PMU), ArduinoJson 7; own
  TCA9554 driver; vendored Espressif es8311 driver.
- Assets: sources in `assets/` with a TOML manifest, converted by
  `tools/assets.py` (lv_font_conv + LVGLImage.py) into committed C files
  in `src/ui/generated/`; big/optional assets go to LittleFS. App icons are
  icon-font glyphs on drawn circles; sprites are sliced sheets played by a
  small helper; big animations are LVGL drawing, not bitmaps.
- Fonts: Inter (text, incl. Czech diacritics), Roboto Mono (timer digits),
  Material Symbols Rounded (icons).
- Runtime: LVGL only in the UI task, an event queue between tasks, an I2C
  mutex, LVGL heap in PSRAM, draw buffers in internal DMA RAM.
- Pure logic in Arduino-free `lib/` modules with host Unity tests.
**Consequences:** Developers need Node (fonts) and Python (images) only when
changing assets; regular builds just compile the generated files.

## 2026-10-09 — Charger behaviour and power-policy precedence

**Context:** Owner's answers on the charger profile.
**Decision:** On the charger, `Normal` apps dim to 40 % after 60 s and go
plain black (display off) after 5 min. No animated screensaver. Non-`Normal`
power policies behave the same on the charger as on battery (e.g. a running
pomodoro uses its minimal view); only the 80 % brightness cap still applies.
Speaker beeps are OK for timer alerts.
**Consequences:** The bot face is no longer the screensaver.

## 2026-10-09 — UI & navigation basics (owner's answers)

**Context:** Open questions from the launcher/apps plan (ARCHITECTURE.md, open questions section).
**Decision:**
- Two side buttons like a modern smartwatch: **Back** (one step, apps can
  handle it) and **Home** (always the menu). BOOT = Back and PWR = Home until
  the physical layout says otherwise (`BUTTON_BACK_IS_BOOT`).
- **Long press** on either button → ~1 s power-off animation → deep sleep;
  the animation gives the user time to release the button so it doesn't
  wake the device again at once.
- Waking from deep sleep always opens the **menu**, not the last app.
- Battery dim stage at **25 %** brightness. Portrait orientation (368 × 448).
- Board confirmed **V2** (CO5300 + CST820).
- App menu: round icons in pairs, vertical scrolling, built for 10+ apps.
- Two power profiles: battery (dim → black → deep sleep) and charger (no
  sleep, max 80 % brightness, burn-in protection).
**Consequences:** Apps get `onBack()`; no last-app restore on wake (apps
still restore their own saved state when opened).

## 2026-10-09 — Default board revision is V2

**Context:** Waveshare ships two revisions (V1: SH8601 + FT3168, V2: CO5300 +
CST820) with identical pins; shipments switched to V2 on 2026-05-30 and the
current product page lists only V2 chips.
**Decision:** `BOARD_REVISION` defaults to `2` in `include/board.h`.
**Consequences:** A V1 board needs `BOARD_REVISION 1` (or runtime detection by
probing touch address `0x15` vs `0x38`, as the vendor BSP does).

## 2026-10-09 — Partition table: 2 × 4 MB OTA + ~8 MB LittleFS

**Context:** 16 MB flash. UI apps with LVGL and assets grow large; OTA needs
two app slots.
**Decision:** `partitions.csv` — nvs 20 KB, otadata, app0/app1 4 MB each,
LittleFS (`spiffs` subtype) 7.9 MB, coredump 64 KB.
**Consequences:** App binary limit is 4 MB; big assets (images, fonts, audio)
go to LittleFS (`data/`) or the microSD card.

## 2026-10-09 — PlatformIO + Arduino core 3.x via pioarduino

**Context:** The owner's other ESP32 projects (WetOrDead, ClaudeObserver) use
PlatformIO + Arduino. The stock PlatformIO espressif32 platform stopped at
Arduino core 2.x; Waveshare's examples need core 3.x (vendor CI uses 3.3.11).
**Decision:** pioarduino platform pinned to a release zip (`55.03.312-1`,
Arduino core 3.3.12), board `esp32-s3-devkitc1-n16r8` (same module: 16 MB QIO
flash, 8 MB OPI PSRAM), USB CDC on boot so `Serial` is the USB-C port.
**Consequences:** Bump the platform URL on purpose and rebuild both envs. If
something needs ESP-IDF-only features, the Arduino core still exposes the IDF
APIs; switching to pure ESP-IDF would use the vendor BSP component
(see docs/HARDWARE.md).

## 2026-10-09 — Secrets fall back to the example file

**Context:** CI and fresh clones have no `include/secrets.h`.
**Decision:** `main.cpp` includes `secrets.h` if present, else
`secrets.example.h` (`__has_include`).
**Consequences:** Builds never fail on missing secrets; every new secret must
get a placeholder in `secrets.example.h`.
