# Roadmap — app launcher + Home Assistant lights

Design: [ARCHITECTURE.md](ARCHITECTURE.md). Work happens on branch
`feature/launcher-home-assistant`, which merges into `main` via a PR.

Each milestone is small enough to flash and check on the board. Agents can't
flash, so every milestone ends with the owner running its **On device**
checklist. Tick items here as they pass, and record surprises in
[LESSONS.md](LESSONS.md).

Status legend: ☐ not started · ◐ in progress · ☑ done

## Phase 0 — Plan ◐

- [x] Architecture and roadmap written
- [ ] Open questions in ARCHITECTURE.md §11 answered by the owner
- [ ] Design reviewed and adjusted → then start M1

## M1 — Display bring-up ☐

Board + display services, no LVGL yet.

- `core/board`: `Wire` init, TCA9554 reset pulse, touch-address probe → log the
  detected revision.
- `core/display`: `Arduino_GFX` CO5300/SH8601 by `BOARD_REVISION`, fill colour,
  `setBrightness()`, panel sleep-in/out helpers.
- Libraries added to `lib_deps` with pinned versions.

**On device:** screen fills with a test colour, no 16 px shift; brightness
steps 255 → 64 → 0 visibly; serial log shows the detected revision.

## M2 — LVGL + touch ☐

- `lv_conf.h`, LVGL 9 display driver with the even-alignment rounder, draw
  buffers in PSRAM, tick source.
- `core/input`: CST820 (V2) / FT3168 (V1) as an LVGL pointer device.
- Demo screen: one button that counts taps.

**On device:** UI renders without tearing or artefacts on partial updates;
taps land where the finger is (all four corners); serial shows FPS ≥ 30
while animating.

## M3 — App framework + launcher ☐

- `apps/app.h` interface, `core/app_manager`, `LauncherApp` with tiles.
- Placeholder "Hello" app to test open/close.
- `ui/theme`: colours, fonts, tile style.

**On device:** tapping a launcher tile opens the app with an animation;
there's a way back (temporary on-screen back button until M4).

## M4 — Side button ☐

- `core/input`: debounced side-button events (short press; long press logged
  only).
- Short press inside an app → back to the launcher.
- Answer to open question §11.1 implemented (BOOT vs. PWR).

**On device:** a press from any app returns to the launcher, every time;
holding the button doesn't repeat; the PWR 6 s hardware power-off still works.

## M5 — Idle dimming ☐

- `core/power`: Active → Dimmed → Black stages with fades, timer reset on
  touch/button, wake locks, the waking touch is swallowed.
- Timings from `config.h`.

**On device:** with no input, 10 s full → 5 s dim → 5 s black (sleep not yet
enabled, stays black); a touch while black restores brightness **without**
clicking anything; a button press restores brightness.

## M6 — Deep sleep + wake ☐

- Sleep entry sequence (ARCHITECTURE §6.2), ext1 wake on the side button,
  `RtcState` in RTC memory, wake cause handled in `setup()`.
- Restore the last app after wake.

**On device:** after 20 s idle the device sleeps (serial stops, panel dark);
a button press wakes it into the same app; a press during the sleep sequence
doesn't immediately re-wake it; holding the button through wake doesn't put the
chip in download mode (GPIO0 is a strapping pin — verify!).
Measure: time from press to first frame (target < 500 ms).

## M7 — Wi-Fi ☐

- `core/net`: connect with `secrets.h` credentials, non-blocking state
  machine, fast reconnect using cached channel/BSSID (and optional static IP),
  status icon in the launcher status bar.

**On device:** cold boot connects < 5 s; wake from deep sleep connects
< 1.5 s; wrong password → crossed-out icon, no hang, UI still responsive.

## M8 — Home Assistant client ☐

- `apps/home_assistant/ha_client`: FreeRTOS task + queues, `GET` state,
  `POST` toggle, ArduinoJson parsing, error mapping (401, 404, timeout).
- Serial-only test hook: log the 4 configured states on connect.

**On device:** the log shows correct on/off for all 4 lights; toggling from
the HA web UI shows up in the log within one poll; a wrong token logs "401".

## M9 — Home Assistant app UI ☐

- 2×2 tile grid, labels from `HA_LIGHTS`, states (on / off / pending /
  n/a / stale), tap to toggle, polling while visible, cached states rendered
  after wake.

**On device:** every tile toggles the right light; the tile shows *pending*
then the real state; the HA web UI agrees; unplugging the HA server → tiles go
stale/n/a without freezing the UI; after deep sleep the tiles appear
instantly (stale) and refresh within ~1.5 s.

## M10 — Polish & merge ☐

- Icons, fonts, transitions; README section on usage and configuration;
  DECISIONS.md entries for the choices made; LESSONS.md updated.
- Measure deep-sleep current and awake current; record them in HARDWARE.md.
- PR `feature/launcher-home-assistant` → `main`.

## Later ideas (not scheduled)

- Brightness slider on long-press of a light tile
- HA WebSocket subscription for instant updates
- Battery % in the status bar (AXP2101)
- Settings app (brightness, sleep timeouts, Wi-Fi info)
- Clock / watch face with the PCF85063 RTC
- Wake on touch
- More than 4 lights / pages, scenes, other entity types
- OTA firmware updates
