# Roadmap

Design: [ARCHITECTURE.md](ARCHITECTURE.md) · app specs: [apps/](apps/README.md).
Large features happen on a `feature/<name>` branch that merges into `main`
via a PR (the launcher and apps: `feature/launcher-home-assistant`, merged;
phone-style power: `feature/phone-power`).

Every milestone is small enough to flash and check on the board. Agents can't
flash, so each one ends with the owner running its **On device** checklist.
Tick items as they pass and record surprises in [LESSONS.md](LESSONS.md).

Order: platform first, then the offline apps (fast feedback, no network),
then Wi-Fi + Home Assistant.

Status: ☐ not started · ◐ code written, on-device check pending · ☑ done

**Where we are (2026-10-18):** M1–M14, M16 (Dice), M17 (Level), M18 (quick
settings), M19 (Timer), M20 (Noise) and M21 (phone-style power, which
replaces the button roles and timings of M4–M7) are implemented. Both firmware envs build with 0 warnings, the
`lib/` logic passes the host unit tests, and the PC simulator taps through
every app (`docs/design/firmware-screens.png`). **Nothing has been flashed
yet**: the next step is the owner working through the **On device**
checklists below, in order, starting with M1. Differences from the plan: no
SensorLib (own touch, RTC and IMU drivers), the panel and ES8311 code is
ported into `src/core/` instead of vendored libraries, and the menu has the
9 real apps instead of 10 placeholders.

## Phase 0 — Plan ☑

- [x] Architecture, app specs, roadmap written
- [x] Most open questions answered (DECISIONS.md, 2026-10-09)
- [x] Remaining questions answered (ARCHITECTURE §13)
- [x] Plan reviewed → start M1

## Platform

### M1 — Display bring-up ◐

`core/board` (Wire + `I2cBus` mutex, own TCA9554 driver with reset pulse,
revision detect by touch address), `core/display` on **esp_lcd QSPI** with the
panel commands ported into `src/core/display.cpp` (CO5300 init from the vendor
BSP): fill, brightness, panel on/off/sleep. Event bus skeleton. `.clang-format`.
If esp_lcd causes trouble, fall back to GFX Library for Arduino
(LIBRARIES.md) and record why in LESSONS.md.

**On device:** test colour fills the screen with no 16 px shift; brightness
steps 255 → 64 → 0 are visible; the log shows the detected revision; the real
corner radius of the panel is noted in HARDWARE.md.

### M2 — LVGL + touch ◐

`lv_conf.h` per UI_DESIGN §8 (LVGL 9.6, `RGB565_SWAPPED`, 2 internal DMA
buffers, async flush, even-alignment rounder, LVGL heap in PSRAM); own
CST820/FT3168 touch driver (`core/touch`) as an LVGL pointer device; `ui/theme` with the colour, spacing and
motion tokens (built-in Montserrat font as a stand-in until M3).

**On device:** no artefacts on partial updates; taps land under the finger in
all four corners; ≥ 30 fps while animating (log LVGL's FPS).

### M3 — Asset pipeline, app framework + scrolling menu ◐

1. **Assets** (UI_DESIGN §7): `assets/` with Inter, Roboto Mono, Material
   Symbols Rounded + licences, `assets.toml`, `tools/assets.py` (+
   `requirements.txt`, `package.json`), generated fonts in `src/ui/generated/`,
   CI check that regenerating produces no diff.
2. **Framework:** `apps/app.h`, `apps/app_manager`, `MenuApp` with round
   icon-font icons in pairs (UI_DESIGN §5),
vertical scroll with row snapping, open/close transitions, remembered scroll
position, with the **9 real apps**. Temporary on-screen back button.

**On device:** smooth scrolling across all rows, snapping to rows,
tap opens the right app, back returns to the same scroll position.

### M4 — Side buttons ◐

`core/pmu` (AXP2101 init, PEK short-press IRQ via EXIO5),
`core/buttons` button events for BOOT (GPIO0, debounced) and PWR, mapped to
**Back** (one step, app `onBack()` first) and **Home** (menu), plus
long-press detection (≥ 1 s) as an event (the sleep action itself comes in M6).
Remove the temporary back button.

**On device:** Back steps back one level, Home always reaches the menu, from
every app; no repeats when held; long press is logged; PWR held 6 s still
powers off. *(The roles changed in M21: BOOT = Power, PWR = Back / Home.)*

### M5 — Power profiles + idle timeline ◐

`core/power`: Battery/Charger profile from `isVbusIn()` with live switching
on VBUS IRQs; per-profile brightness caps and dim/black timings; wake locks
and `PowerPolicy` (policies beat profiles); the waking touch is swallowed.
Status bar: battery % or ⚡. The state machine lives in `lib/PowerFsm`, which
brings in `[env:native]` + the first Unity tests and a CI test job.

*(Timings and the Off stage changed in M21.)*

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

*(The wake path changed in M21: a button wake reopens the last app, the
idle timeline ends in the screen going off, and the apps decide the sleep.)*

**On device:** sleeps after 20 s idle on battery, never on the charger by
idle; long press sleeps in any profile and the held button doesn't
immediately wake it; BOOT wakes into the menu; holding BOOT through the wake doesn't enter download
mode; PWR result recorded in HARDWARE.md and LESSONS.md; time to first
frame measured (target < 500 ms); deep-sleep current measured (HARDWARE.md,
Power → Deep-sleep current; goal ≥ a month asleep, System → Standby agrees).

### M7 — Burn-in protection ◐

Pixel shift in the display driver (Lissajous, ±4 px every 3 min), 4 px safe
margins in the theme, charger plain-black stage (display off after 5 min idle).

*(The charger's plain-black stage became the screen going off after
10 min in M21.)*

**On device:** the shift is visible but not distracting; on the charger the
screen goes black after 5 min and a touch restores the app.

### M21 — Phone-style power ◐

BOOT = Power (press: CRT animation, screen off; hold 1.5 s: shutdown
animation, power off), PWR = Back (press) / Home (hold). Stages Active →
Dimmed → Off (battery 30 / 40 s, charger 2 / 10 min) or → Minimal.
`App::sleepNeed()` merged and decided by `lib/SleepPlan`: doze (80 MHz,
light sleep), deep sleep with a deadline timer, or deep sleep until the
button. Timer wakes boot dark; `manager::attention()` lights the screen.
RTC-memory parking (`store::park()`), `core::mono()` across deep sleep, the
last app reopened on a button wake, power-off restarts in the menu. IMU
powered down outside Level, touch asleep with the screen, SD untouched, AXP
ADCs off asleep. Bot face 1 min and lively; System → Touch → Reset to
factory; quick settings show the Wi-Fi state. ([DECISIONS](DECISIONS.md)
2026-10-18, ARCHITECTURE §2.2, §8.)

**On device:**
- [ ] Which side button is BOOT? The power button must be BOOT (only it
      wakes the chip); if BOOT is the lower one, tell the agent.
- [ ] Power press: the CRT animation ends on a fully black screen (no strip,
      no glow); the screen is off and touch does nothing; Power brings back
      the same app in < 0.5 s, from deep sleep too (Dice shows its last roll,
      the menu its scroll position).
- [ ] Power held 1.5 s: the ring animation; afterwards only Power starts it,
      into the menu; a stopwatch that ran is paused with its time.
- [ ] Holding BOOT through a wake doesn't enter download mode and doesn't
      power off.
- [ ] PWR press = Back, PWR held 1 s = Home; neither wakes the screen off.
- [ ] Idle on battery: dim at 30 s, off at 40 s; on the charger 2 min /
      10 min, and Power wakes it at once (doze).
- [ ] A 2-minute countdown, screen off by Power: the device deep-sleeps,
      wakes at the end with the alarm and the Timer in front; Power silences
      it. A pomodoro phase end does the same with its beep.
- [ ] Noise playing, screen off: the rain goes on (doze); stopping it from
      the app lets the next screen-off deep-sleep.
- [ ] Level: the IMU is powered only while it's on screen (the debug log's
      `imu: raw` lines stop with the screen off).
- [ ] Currents (HARDWARE.md, Power): deep sleep, power-off, dozing with
      noise, the minimal clock; System → Standby after a day or two.

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

### M19 — Timer ◐ — [spec](apps/timer.md)

Clock / stopwatch / countdown tabs, `lib/TimerCore` (host-tested), the
clock's minimal view (160 px digits, 25 % via `App::minimalBrightness()`,
moving every minute), countdown alarm.

**On device:** spec acceptance list, including the minimal clock's current.

### M20 — Noise ◐ — [spec](apps/noise.md)

`lib/NoiseGen` (rain, white, blue; host-tested), noise streaming with fades
in `core::sound`, playing keeps the chip dozing with the screen off (M21; first
a policy `NoSleep`), rain / snow / shimmer animations; volume
and mute in quick settings.

**On device:** spec acceptance list; listen to the rain first.

### M22 — Magic 8-ball ◐ — [spec](apps/magic8.md)

`lib/EightBall` (22 answers with their colours, the shake detector;
host-tested), text only in Permanent Marker, rattling out and rising in;
IMU on only while it's on screen.

**On device:** spec acceptance list; does a real shake trigger it, and a
tilt not?

### M23 — Hephy ◐ — [spec](apps/hephy.md)

`lib/PetCore` (meters, forge, shelf, xp on the wall clock; host-tested),
`lib/BotFace` moods and reactions (asleep, sad, focused, chew, shake head,
startle) and `scaled()`; Main, Strike, Shelf and the reveal; NVS only, no
wakes.

**On device:** spec acceptance list; a forge across a power-off; the RTC
keeping time through deep sleep.

### M24 — 2048 ◐ — [spec](apps/g2048.md)

`lib/Game2048` (slides, merges, the moves to animate; host-tested), one
object per tile, slide → pop/grow in two steps, swipe on the board.

**On device:** spec acceptance list; do fast swipes all register?

## Networked apps

### M11 — Wi-Fi ◐

`core/net`: connect on demand, non-blocking, fast reconnect (per-network
channel/BSSID and DHCP lease, optional static IP), released when no app needs it on
battery; Wi-Fi icon in the status bar.

**On device:** cold connect < 5 s; reconnect after wake < 1.5 s; a wrong
password doesn't hang the UI.

### M12 — OTA + System app ◐ — [spec](apps/system.md)

`tools/version.py` (git-describe version), CI firmware size check (warn
3.0 MiB / fail 3.5 MiB), `[env:amoled-ota]` (espota, password from
`ESP_OTA_PASSWORD`), VS Code task "ESP: upload OTA (Wi-Fi)", System app with the
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

### M18 — Quick settings + Wi-Fi setup ◐ — [spec](apps/quick-settings.md), [Wi-Fi](apps/wifi.md)

Shade pulled down from the menu's status bar with a Wi-Fi row, a live
brightness bar (5–70 %, saved in NVS) and a volume bar with mute; Wi-Fi
screen with scan, Nokia-style multi-tap keypad (`lib/MultiTap`, host-tested) and a trial join that saves
the network only once it connects (`core::settings`, `net::join()`).

**On device:** both specs' acceptance lists.

## Wrap-up

### M15 — Polish & merge ◐

Icons and fonts, menu fisheye effect (optional), README usage section,
DECISIONS/LESSONS updated, currents recorded in HARDWARE.md, then the PR into
`main`.

## Later

Settings app (timeouts), HA WebSocket push, IMU features (bot reactions, espresso auto-start), OTA pull from GitHub
Releases, more apps from [apps/README.md](apps/README.md). Measure the power
figures in [POWER.md](POWER.md) and cut the screen-on cost if needed (CPU
frequency scaling between animations).
