# Decisions

Why the project is set up the way it is. Newest first. Add an entry when you
make a choice someone might later question.

Format: `## YYYY-MM-DD — title`, then *Context*, *Decision*, *Consequences*.

## 2026-10-18 — Phone-style power: BOOT is Power, apps say what they need asleep

**Context:** The owner wants the device to behave like a phone while staying
very frugal: one button turns the screen off (press) or powers off (hold),
the other is Back (press) and Home (hold); apps that need to keep working
with the screen off (rain sound, a running countdown) should keep doing so,
everything else should sleep as deeply as possible, and a wake should bring
back the app that was open. Power-off should look different from screen-off
and never leave pixels lit (the CRT animation left a strip and a glow).
Peripherals should be on only while used, the screen should stay on longer,
and the burn-in rules could relax a little.
**Decision:**
- **BOOT is the power button**, PWR is Back / Home. Only BOOT (GPIO0, an RTC
  GPIO) can wake the chip; PWR's IRQ ends at the TCA9554. The owner called
  the top button the one that boots it; if the top one is PWR after all, the
  roles still can't swap without losing the wake (open question in
  ARCHITECTURE §13). Replaces `BUTTON_BACK_IS_BOOT` and the 2026-10-09
  smartwatch mapping.
- **A standard question for every app**: `sleepNeed()` → keep the chip
  awake, and/or wake it at a time. `lib/SleepPlan` turns the merged answer
  into doze (CPU up at 80 MHz, light sleep between passes; sound, the
  charger, or a deadline < 5.6 s away), deep sleep with a timer (a later
  deadline, 0.6 s early), or deep sleep until the button. A deadline wake
  boots with the panel dark and goes back to sleep after 0.4 s unless an app
  calls `manager::attention()`. Apps park their running state in RTC memory
  (`onSleep()`, `store::park()`), restore it in `onWake()`, and a button wake
  reopens the app that was on screen.
- **`core::mono()`**, a millisecond clock that carries on through deep sleep
  (offset + wall-clock time slept, the PCF85063 being the truth), so
  TimerCore and PomodoroCore objects are parked byte for byte and stay
  right: no "remaining time" conversions, no new library code.
- **Power-off is a deep sleep with only the button as wake source**, timers
  paused and sound stopped, restarting in the menu. Not an AXP2101 shutdown:
  only PWR can end that, and it would need the button roles swapped. The
  rails stay up (unknown mapping, no schematic); the AXP's voltage ADCs and
  the IMU's oscillator go off.
- **Peripherals on demand**: the IMU is powered down (CTRL1 sensor disable)
  except while Level is on screen; the touch controller sleeps with the
  screen (a reset pulse brings it back with the panel); the SD slot is no
  longer touched (no card, ever, for now).
- **Longer screen times**: battery dim 30 s / off 40 s (was 10 / 15 / sleep
  20 s), charger 2 min / 10 min (was 1 min / 5 min black), bot face 1 min,
  espresso 1 min. The bot face wanders and does idle acts so it never sits
  still.
**Consequences:** Deep sleep is entered often (every screen-off on battery
with nothing running), so its entry and the boot must stay fast; the reboot
costs ~0.3–0.5 s of ~40 mA per wake, which is why close deadlines doze. The
power button is the only way to wake the screen: touch, Back and Home do
nothing with the screen off. NVS is written only at a power-off, not at every
sleep. Nothing of this is verified on the board yet (ROADMAP M21).

## 2026-10-15 — Landscape for the Level app only, by turning the LVGL display

**Context:** The owner wants the UI to turn to landscape, but only in the
Level app and only standing on the left edge (the flat one; the buttons are
on the right). Everything else is laid out for 368 × 448.
**Decision:** The app manager turns the whole LVGL display
(`LV_DISPLAY_ROTATION_270`) while the open app's `landscape()` is true, and
never for the menu or a minimal view. LVGL then lays out 448 × 368 and turns
touch; the flush callback turns each rendered band into panel orientation
with `lv_draw_rotate` (one extra 29 KB DMA buffer, allocated on first use).
Level lays itself out again in `onRotated()`. Not `transform_rotation` on
the app's root (renders through a full-screen layer) and not rotated glyphs
(LVGL turns letters, not lines of text).
**Consequences:** Any app can opt in later with its own landscape layout.
Leaving the app empties it before the screen turns back, so the landscape
layout never fades out sideways. The power-off animation sizes itself from
the current resolution.

## 2026-10-15 — A month in deep sleep: power the codec down, measure the drain

**Context:** The owner wants the device to last at least a month in deep
sleep. Nobody had measured the sleep current, and a review of the sleep
path found two leaks: the ES8311's analog side stayed powered after the
first beep (nothing ever powered it down), and a long press during Noise
went to sleep mid-fade with the codec still running.
**Decision:**
- The codec powers down whenever the output closes (es8311_suspend()
  sequence) and is initialised again on the next sound. Sleep entry waits
  up to 1 s for noise to fade out.
- A budget and a measuring procedure in HARDWARE.md (≤ ~450 µA average for
  30 days on 400 mAh). The boot log lists the AXP2101 rails that stay on.
- The device measures its own sleep drain (`lib/SleepLog`): gauge level and
  wall clock at sleep and at wake, added up until the next charge, shown as
  System → Standby. No current meter needed for a first answer.
- Not "deep off" (AXP power-off, ARCHITECTURE §8.4 Option B): it would cut
  the drain to the AXP's own few µA but lose BOOT wake and RTC memory. Only
  worth it if the measured standby falls short.
**Consequences:** A re-init of the codec (~30 I2C writes, a few ms) before
each sound. The Standby figure needs 2 % lost or a day asleep before it
says anything, and is only as good as the AXP2101's gauge.

## 2026-10-10 — Timer and Noise apps; volume and mute in quick settings

**Context:** The owner asked for a Timer (a big presentation clock that must
never blank but mustn't burn in, plus stopwatch and a countdown up to
99:59) and a Noise app (rain with a rain animation; swipe for white or blue
noise) that may run for hours. Noise needed a volume and mute setting.
**Decision:**
- The clock tab is `LowPowerDisplay`: after the dim delay it shows a dim
  minimal clock (160 px hours over minutes, about 5 % of the pixels) that
  moves every minute, with light sleep between minute redraws. Apps may now
  ask for a brighter minimal view (`App::minimalBrightness()`); the Timer
  uses 25 % instead of 15 % so the clock reads across a room.
- Noise is generated live (`lib/NoiseGen`), not stored: rain is band-passed
  pink noise with gusts plus synthesised drops; blue is differenced pink.
  `core::sound` streams it from its task in 16 ms buffers with fades, and
  plays beeps in between.
- New policy `NoSleep` (the Normal timeline without deep sleep) for Noise
  while playing: the screen still dims and goes black, which is the best
  burn-in protection, and the sound goes on. Light sleep is skipped while the
  I2S output runs, so a minimal view (Pomodoro, Timer) and noise coexist.
- Volume (0–100 %, 40 dB span) and mute are settings in NVS (key `volume`),
  replacing the compile-time `SOUND_VOLUME`. Mute drops beeps and keeps the
  amplifier off.
**Consequences:** While noise plays the chip never sleeps: hours of battery,
not days; a sleep timer is the obvious next step. Running timers stop on a
long-press power-off (only the countdown's set time is saved). The minimal
brightness check in `power.cpp` now compares against the capped level, which
also fixes a minimal view that never light-slept when the user brightness
was below 15 %.

## 2026-10-10 — Quick settings: Wi-Fi and brightness set on the device

**Context:** Changing networks or brightness meant editing `secrets.h` /
`config.h` and reflashing. The owner asked for an Android-style shade on the
menu with exactly two settings: Wi-Fi (scan, password on a Nokia-style
keypad) and brightness (5–70 %, live), both remembered.
**Decision:** `core::settings` keeps them in NVS (keys `bright`, `wifi`),
which firmware updates don't touch. A saved network wins over `secrets.h`.
`net::join()` tries a new network and saves it only once it connects, so a
typo never replaces a working network. The brightness setting replaces
`BRIGHTNESS_MAX_BATTERY` / `_CHARGER`: it is the active level on both
profiles, capped at 70 % (the old charger cap's burn-in reasoning), and
dimmed is at most half of it. Multi-tap typing is a host-tested library
(`lib/MultiTap`). The Wi-Fi screen is an `App` that isn't in the menu.
**Consequences:** The password sits in NVS in plain text (no flash
encryption), as `secrets.h` sits in the image. "Erase flash" forgets both
settings. Typing a 20-character password takes a minute; QR or a phone-side
setup could come later.

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

## 2026-10-15 — Several known Wi-Fi networks, strongest in range first

**Context:** One saved network (plus `WIFI_SSID`) wasn't enough for a device
that moves between home, office and a phone hotspot.
**Decision:** Up to five networks in NVS (`wifis`, most recently joined
first) plus `WIFI_SSID` and an optional `WIFI_EXTRA_NETWORKS` list in
`secrets.h`. With one known network `net` joins it directly (no scan, as
before); with several it scans and tries them strongest first, then waits
`WIFI_RETRY_MS` before scanning again. After deep sleep the cached channel /
BSSID of the last network skips the scan. Not Arduino's `WiFiMulti`: its
`run()` blocks for the whole scan and connect, and the UI loop must not.
**Consequences:** With several networks a cold connect costs one scan
(~2–3 s) more. The old single `wifi` blob is read once and replaced by
`wifis` on the next save.

## 2026-10-15 — Per-network channel, access point and lease; one network in secrets.h

**Context:** The day's earlier multi-network change kept one deep-sleep cache
(RTC) for the last network only and added `WIFI_EXTRA_NETWORKS` to
`secrets.h`. Extra networks are better added on the device, and each of them
should reconnect as fast as the secrets one with a static IP.
**Decision:** `secrets.h` has one network again (`WIFI_EXTRA_NETWORKS` is
gone). Every network that connects is saved in NVS (`wifinets`) with its
channel, BSSID and, when it came from DHCP, the lease and its time. The
first connect goes to the last network on its channel and BSSID, reusing the
lease as a static IP while younger than `WIFI_LEASE_REUSE_S` (12 h) and the
clock is valid; any failure falls back to a normal join with DHCP (and a scan
with several networks). The RTC Wi-Fi cache is removed: NVS covers deep
sleep and power-off alike.
**Consequences:** Reconnects skip both the scan and DHCP. A reused lease isn't
confirmed with the router, so an IP clash is possible if the router reassigns
it within 12 h; a DHCP reservation avoids that. NVS is written on a connect
only when the channel, BSSID or lease changed (at most about once per lease).

## 2026-10-15 — Lights come from Home Assistant, not from config.h

**Context:** `HA_LIGHTS` in `config.h` had to list exactly four entity ids by
hand, and a bare `HA_BASE_URL` host ("homeassistant") never resolved.
**Decision:** The app loads every `light.*` entity from `GET /api/states`
(names from `friendly_name`, sorted by name, the first `HA_MAX_LIGHTS` = 16),
reloads it every `HA_LIST_MS`, and caches it in NVS. The body is read with
HTTP/1.0 and parsed off the socket one entity at a time, so a big house costs
no memory. Not `POST /api/template` (a tiny answer, but it needs an admin
token, and the device should use a non-admin user) and not the WebSocket API
(later, for push updates). A bare host name that DNS doesn't know is tried as
`<name>.local`.
**Consequences:** No light configuration on the device; all lights show
(choosing some, e.g. by HA area or label, is for later). The list request
transfers the whole state list, so it runs once a minute, not every poll.

## 2026-10-15 — Dice edges are rasterised by us, not by LVGL

**Context:** 2 × 10 d20 ran at about 2 fps: 600 anti-aliased edges per frame,
and LVGL's skewed-line drawing masks every pixel of each line's bounding box
(up to four masks per pixel), repeated for every 40-line band.
**Decision:** `lib/WireRaster` draws the edges into a stage-sized RGB565
buffer in PSRAM once per frame, touching only the pixels each line covers;
the stage copies the buffer with `lv_draw_image` and LVGL still draws the
landed faces, numbers and totals on top. Not a bigger LVGL draw buffer (no
internal RAM to spare, and the per-pixel cost stays) and not dropping the
hidden edges (they carry the 3D look).
**Consequences:** ~220 KB of PSRAM while Dice is open (freed on leaving).
Host benchmark 3.5 → 0.8 ms per frame; on the board 2 × 10 d12 (also 30
edges per die) went from about 2 to 16.7 fps. The buffer is in the display's
pixel format, so the copy needs no conversion.
