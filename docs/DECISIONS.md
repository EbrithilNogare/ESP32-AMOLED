# Decisions

Why the project is set up the way it is. Newest first. Add an entry when you
make a choice someone might later question.

Format: `## YYYY-MM-DD — title`, then *Context*, *Decision*, *Consequences*.

## 2026-10-09 — UI & navigation basics (owner's answers)

**Context:** Open questions from the launcher/apps plan (ARCHITECTURE.md §12).
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
