# Roadmap

Design: [ARCHITECTURE.md](ARCHITECTURE.md) · app specs: [apps/](apps/README.md).
Work happens on branch `feature/launcher-home-assistant`, which merges into
`main` via a PR.

Every milestone is small enough to flash and check on the board. Agents can't
flash, so each one ends with the owner running its **On device** checklist.
Tick items as they pass and record surprises in [LESSONS.md](LESSONS.md).

Order: platform first, then the offline apps (fast feedback, no network),
then Wi-Fi + Home Assistant.

Status: ☐ not started · ◐ code written, on-device check pending · ☑ done

**Where we are (2026-10-09):** M1–M14 are implemented in one go on this
branch. Both firmware envs build with 0 warnings, the `lib/` logic passes 98
host unit tests, and the PC simulator taps through every app
(`docs/design/firmware-screens.png`). **Nothing has been flashed yet**: the
next step is the owner working through the **On device** checklists below, in
order, starting with M1. Differences from the plan: no SensorLib (own touch
and RTC drivers), the panel and ES8311 code is ported into `src/core/`
instead of vendored libraries, and the menu has the 5 real apps instead of
10 placeholders.

## Phase 0 — Plan ☑

- [x] Architecture, app specs, roadmap written
- [x] Most open questions answered (DECISIONS.md, 2026-10-09)
- [x] Remaining questions answered (ARCHITECTURE §13)
- [x] Plan reviewed → start M1

## Platform

### M1 — Display bring-up ◐

`core/board` (Wire + `I2cBus` mutex, own TCA9554 driver with reset pulse,
revision detect by touch address), `core/display` on **esp_lcd QSPI** with the
vendored panel driver in `lib/esp_lcd_amoled/` (CO5300 init from the vendor
BSP): fill, brightness, panel on/off/sleep. Event bus skeleton. `.clang-format`.
If esp_lcd causes trouble, fall back to GFX Library for Arduino
(LIBRARIES.md) and record why in LESSONS.md.

**On device:** test colour fills the screen with no 16 px shift; brightness
steps 255 → 64 → 0 are visible; the log shows the detected revision; the real
corner radius of the panel is noted in HARDWARE.md.

### M2 — LVGL + touch ◐

`lv_conf.h` per UI_DESIGN §8 (LVGL 9.6, `RGB565_SWAPPED`, 2 internal DMA
buffers, async flush, even-alignment rounder, LVGL heap in PSRAM); touch via
SensorLib as an LVGL pointer device; `ui/theme` with the colour, spacing and
motion tokens (built-in Montserrat font as a stand-in until M3).

**On device:** no artefacts on partial updates; taps land under the finger in
all four corners; ≥ 30 fps while animating (log LVGL's FPS).

### M3 — Asset pipeline, app framework + scrolling menu ◐

1. **Assets** (UI_DESIGN §7): `assets/` with Inter, Roboto Mono, Material
   Symbols Rounded + licences, `assets.toml`, `tools/assets.py` (+
   `requirements.txt`, `package.json`), generated fonts in `src/ui/generated/`,
   CI check that regenerating produces no diff.
2. **Framework:** `apps/app.h`, `core/app_manager`, `MenuApp` with round
   icon-font icons in pairs (UI_DESIGN §5),
vertical scroll with row snapping, open/close transitions, remembered scroll
position. **10 placeholder apps** to exercise scrolling. Temporary on-screen
back button.

**On device:** smooth scrolling (no stutter with 10+ icons), snapping to rows,
tap opens the right app, back returns to the same scroll position.

### M4 — Side buttons ◐

`core/pmu` (AXP2101 init, PEK short-press IRQ via EXIO5),
`core/input` button events for BOOT (GPIO0, debounced) and PWR, mapped to
**Back** (one step, app `onBack()` first) and **Home** (menu), plus
long-press detection (≥ 1 s) as an event (the sleep action itself comes in M6).
Remove the temporary back button.

**On device:** Back steps back one level, Home always reaches the menu, from
every app; no repeats when held; long press is logged; PWR held 6 s still
powers off.

### M5 — Power profiles + idle timeline ◐

`core/power`: Battery/Charger profile from `isVbusIn()` with live switching
on VBUS IRQs; per-profile brightness caps and dim/black timings; wake locks
and `PowerPolicy` (policies beat profiles); the waking touch is swallowed.
Status bar: battery % or ⚡. The state machine lives in `lib/PowerFsm`, which
brings in `[env:native]` + the first Unity tests and a CI test job.

**On device:** on battery 10 s full → 5 s dim → 5 s black (no sleep yet);
plugging in the charger switches the profile live (max 80 %, dim after 60 s);
the battery % looks plausible.

### M6 — Deep sleep + button wake ◐

Sleep entry sequence, `RtcState`, wake cause handling, wake → menu with a
power-on animation. Long press on either button → power-off animation
(CRT collapse, ~1 s) → wait for release → deep sleep.
1. BOOT wake via ext1.
2. **Experiment:** does PWR reach any RTC GPIO (Option A, ARCHITECTURE §8.4)?
   If yes, PWR wakes too; if not, BOOT alone is fine (decided).

**On device:** sleeps after 20 s idle on battery, never on the charger by
idle; long press sleeps in any profile and the held button doesn't
immediately wake it; BOOT wakes into the menu; holding BOOT through the wake doesn't enter download
mode; PWR result recorded in HARDWARE.md and LESSONS.md; time to first
frame measured (target < 500 ms); deep-sleep current measured.

### M7 — Burn-in protection ◐

Pixel shift in the display driver (Lissajous, ±4 px every 3 min), 4 px safe
margins in the theme, charger plain-black stage (display off after 5 min idle).

**On device:** the shift is visible but not distracting; on the charger the
screen goes black after 5 min and a touch restores the app.

## Offline apps

### M8 — Bot face ◐ — [spec](apps/bot-face.md)

Port of the approved mockup face (spec has the exact geometry, timings and
expression presets): `lib/BotFace` logic with host tests, LVGL drawing in
`src/apps/bot_face/`, partial redraw of the face area only.

**On device:** spec acceptance list, including a side-by-side check with the
mockup.

### M9 — Espresso shot timer ◐ — [spec](apps/espresso-timer.md)

State machine (host-tested), big digits, colour thresholds, history in NVS,
`KeepAwake` while running, longer dim delay in this app.

**On device:** spec acceptance list.

### M10 — Light sleep + sound, then Pomodoro ◐ — [spec](apps/pomodoro.md)

1. `LowPowerDisplay` support in PowerManager: minimal-mode switch,
   timer-driven refresh, ESP light sleep between refreshes with touch and
   button wake.
2. `core/sound`: ES8311 beep patterns, amp gated by GPIO46.
3. Pomodoro app: state machine (host-tested), full and minimal views,
   background running.

**On device:** spec acceptance list, plus the measured current in minimal mode.

## Networked apps

### M11 — Wi-Fi ◐

`core/net`: connect on demand, non-blocking, fast reconnect (cached
channel/BSSID, optional static IP), released when no app needs it on
battery; Wi-Fi icon in the status bar.

**On device:** cold connect < 5 s; reconnect after wake < 1.5 s; a wrong
password doesn't hang the UI.

### M12 — OTA + System app ◐ — [spec](apps/system.md)

`tools/version.py` (git-describe version), CI firmware size check (warn
3.0 MB / fail 3.5 MB), `[env:amoled-ota]` (espota, password from
`ESP_OTA_PASSWORD`), VS Code task "ESP: upload OTA", System app with the
info list and the update screen, rollback via `verifyRollbackLater()` and a
health check (ARCHITECTURE §12).

**On device:** spec acceptance list, including power-pull and rollback tests.
From here on, day-to-day flashing can be wireless.

### M13 — Home Assistant client ◐

`HaClient` task + queues, GET state / POST toggle, JSON parsing
(host-tested), error mapping.

**On device:** the log shows correct states for the 4 lights; a wrong token
logs 401.

### M14 — Home Assistant app ◐ — [spec](apps/home-assistant.md)

2×2 tiles, toggle, polling, stale cache after wake.

**On device:** spec acceptance list.

### M16 — Dice ◐ — [spec](apps/dice.md)

`lib/DiceCore` (rolls, advantage, percentile d100, solids with face-on rest
pose, spin, projection) with host tests; `DiceApp` draws wireframe dice in
the stage's draw event; NVS keeps the last die/count/mode.

**On device:** the checklist in the spec (smooth spins with 20 dice, every
die lands face-on, controls don't roll, settings survive deep sleep).

### M17 — Level ◐ — [spec](apps/level.md)

Own QMI8658 driver (`core/imu`, accelerometer only, standby when unused),
`lib/LevelCore` (angles, flat/edge switching, smoothing) with host tests, and
`LevelApp`: bullseye vial flat, attitude horizon with lean on an edge, Zero /
Hold / ° %.

**On device:** the checklist in the spec — start with the IMU axis mapping
(`IMU_SCREEN_*` in `config.h`), which hasn't been checked on the board.

## M15 — Polish & merge ◐

Icons and fonts, menu fisheye effect (optional), README usage section,
DECISIONS/LESSONS updated, currents recorded in HARDWARE.md, then the PR into
`main`.

## Later

Settings app (brightness, timeouts), clock/watch face (RTC), battery/system
info app, HA WebSocket push, IMU features (bot reactions, espresso
auto-start), OTA pull from GitHub Releases, more apps from [apps/README.md](apps/README.md).
