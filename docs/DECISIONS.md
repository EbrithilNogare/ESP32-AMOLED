# Decisions

Why the project is set up the way it is. Newest first. Add an entry when you
make a choice someone might later question.

Format: `## YYYY-MM-DD — title`, then *Context*, *Decision*, *Consequences*.

## 2026-10-10 — Review pass: wrap-safe time, shared widgets, warnings on

**Context:** A full review of the PR before merging found the same few
patterns in many places: `now >= deadline` comparisons that break when
`millis()` wraps (49.7 days, reachable on the charger), hand-built copies of
the same toast / ring / segmented control / small button in several apps,
and no compiler warnings enabled for `src/`.
**Decision:** Deadlines go through `core::reached()` and PomodoroCore gets a
64-bit clock from `core::extend()` (`src/core/ticks.h`); the simulator starts
30 s before the wrap so the flow test crosses it. Shared widgets and layout
constants live in `src/ui/theme.h` (`header`, `textButton`, `segmented`,
`toast`, `progressRing`, `kEdge`, `kBarBottom`, `kBtn`, `kBtnSmall`), and
`ui::box()` is non-clickable. `-Wall -Wextra` applies to `src/` in the device
envs and to the apps and simulator in `[env:sim]` (not to libraries).
**Consequences:** New apps get consistent headers and bars for free; a
deadline reset must use the current time, not 0 (LESSONS). Warnings show up
in CI builds, which must stay at zero.

## 2026-10-10 — Level: bullseye flat, horizon on an edge; own IMU driver

**Context:** Five Level drafts (bullseye, horizon, carpenter tubes,
protractor, marble). The owner picked the bullseye for lying flat and the
attitude horizon for standing on an edge, with the up/down lean added.
**Decision:** One app that switches view from the gravity vector (40° / 50°
hysteresis). The QMI8658 gets a ~60-line driver in `core/imu` on the shared
I2C bus (accelerometer only, standby when unused) instead of SensorLib, in
line with the touch and RTC drivers. The screen axis mapping is a set of
`IMU_SCREEN_*` macros because the chip's orientation on this PCB isn't
documented; it is the first item of the on-device checklist. The gyroscope
isn't used until the board shows the accelerometer alone is too jumpy.
**Consequences:** Low power (the IMU sleeps outside the app), testable maths
in `lib/LevelCore`; the axis mapping may need a one-line fix after flashing.

## 2026-10-09 — Dice: wireframe design, drawn in the draw event

**Context:** Five dice designs were drafted (tumbling silhouettes, a physics
tray, slot reels, 3D wireframes, a dial). The owner picked the wireframes,
with an orange colour set in `config.h`, green Adv / red Dis, and dice that
must land exactly face-on.
**Decision:** The spin is a rotation matrix `rest × axisAngle(axis, θ(1−ease))`
that reaches the rest pose exactly, so the last frame is always perfectly
face-on. The dice are drawn with `lv_draw_line` / `lv_draw_triangle` /
`lv_draw_label` inside the stage object's `LV_EVENT_DRAW_MAIN` instead of an
`lv_canvas`: no 200 KB pixel buffer, and LVGL only re-renders the stage.
Projection runs once per frame in `onTick`, not per draw chunk.
**Consequences:** `LV_USE_CANVAS` stays off. Up to 20 solids × 30 edges are
re-drawn per frame while spinning (checked in the simulator, to be measured
on the board, see the spec's checklist).

## 2026-10-09 — Own small drivers instead of SensorLib and vendored libs

**Context:** The plan used SensorLib for touch/RTC and vendored
`esp_lcd_sh8601` + `es8311` into `lib/`. During implementation each turned
out to need only a few dozen lines: touch is one 7-byte read (same layout on
CST820 and FT3168), the RTC one BCD read/write, the panel only the QSPI
framing plus an init table, and the vendor ES8311 driver uses the legacy I2C
driver, which conflicts with `Wire`.
**Decision:** Own drivers in `src/core/` (`touch`, `rtc_clock`, `display`,
`sound`), with the origin and licence noted in the file header where code
was ported. XPowersLib stays (the AXP2101 has many registers).
**Consequences:** Two fewer dependencies and all I2C goes through one locked
bus. The QMI8658 IMU has no driver yet; SensorLib is the first candidate
when an app needs it. (Superseded 2026-10-10: own `core/imu` driver, see the
Level entry.)

## 2026-10-09 — PC simulator as the UI test

**Context:** Agents can't flash the board, and LVGL layout bugs are cheap to
find on the PC but slow to find on hardware.
**Decision:** `[env:sim]` compiles the real `src/apps` + `src/ui` + LVGL for
the host, with the hardware modules stubbed in `tools/sim/`. A scripted flow
test taps through every app, asserts state, and saves frames; CI runs it and
uploads the screenshots. `docs/design/firmware-screens.png` is the
committed result.
**Consequences:** Apps must reach hardware only through `src/core` headers
(so the stubs can stand in). Timing, DMA, touch accuracy and power still
need the on-device checklists.

## 2026-10-09 — Settings service deferred

**Context:** ARCHITECTURE §6.3 plans a `Settings` service (NVS overrides of
`config.h`).
**Decision:** Not built until there's a Settings app that writes overrides;
apps read `config.h` directly meanwhile.
**Consequences:** Adding Settings later means routing the tunables it
exposes through the service — a mechanical change.

## 2026-10-09 — Bot face design approved

**Context:** The owner liked the bot face in the UI mockup.
**Decision:** The mockup's face is the reference design: cyan `#00E5FF`
rounded-rectangle eyes and a curved mouth on black, with blink, glance,
breathe, happy/surprised/sleepy expressions. Its exact geometry, timings and
presets are written into `docs/apps/bot-face.md`.
**Consequences:** M8 is a port of the mockup, not a new design; changes to
the face go into the mockup and the spec together.

## 2026-10-09 — Last open questions closed

**Decision:** Keep BOOT = Back, PWR = Home. BOOT alone as the deep-sleep wake
button is enough; PWR wake is a bonus if M6 shows the hardware allows it,
and the AXP2101 power-off ("deep off") mode won't be built. OTA listens only
while the update screen is open.
**Consequences:** No `SLEEP_MODE_DEEP_OFF` or `OTA_LISTEN_ON_CHARGER`
options; fewer code paths to test.

## 2026-10-09 — OTA updates (plan)

**Context:** The owner wants wireless updates if the firmware stays within the
limits. The partition table already has two 4 MB OTA slots.
**Decision:** v1 is push OTA from the PC (ArduinoOTA/espota), started from a
System app screen that keeps the device awake. App rollback uses the core's
bootloader (rollback enabled) plus a health check before the new image is
marked valid. Firmware budget ≤ 3.0 MiB target, 3.5 MiB hard limit, checked
in CI. Version from `git describe`. Pulling from GitHub Releases comes later.
**Consequences:** Every milestone keeps the image inside the budget; big
assets go to LittleFS. The listener never runs without a password, and only
while System → Wireless update is open (owner's choice: no background
listening, not even on the charger).

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
(Partly superseded 2026-10-09 — own drivers instead of SensorLib and vendored
libraries.)

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
**Decision:** The modules that need credentials (`core/net`, `core/ota`,
`ha_client`) include `secrets_or_example.h`, which picks `secrets.h` if
present, else `secrets.example.h` (`__has_include`). The example SSID is
empty and `net` never starts the radio without one.
**Consequences:** Builds never fail on missing secrets; every new secret must
get a placeholder in `secrets.example.h`. A build without secrets stays
offline instead of retrying a fake network.
