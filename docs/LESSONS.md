# Lessons learned

Gotchas from real hardware, libraries, and the toolchain. Add one whenever
something cost time to figure out. Newest first.

Format: `## Short symptom` — what happened, the cause, the fix.

## A deadline of 0 never fires once `millis()` passes 2³¹

Deadlines are compared wrap-safe with `core::reached(now, deadline)`
(`src/core/ticks.h`, a signed difference), because `millis()` wraps after
49.7 days and a board on the charger gets there. With that compare, `0` no
longer means "fire now": from 24.8 days of uptime on, `now - 0` is negative
and the poll / frame / status refresh waits another 24.8 days. Reset
deadlines to the current time, never to 0. The simulator clock starts 30 s
before the wrap, so the flow test runs across it and caught this.

## `gpio_wakeup_enable()` turns a live edge ISR into an interrupt storm

Arming the touch INT pin as a light-sleep wake source
(`gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)`) switches the pin's live
`attachInterrupt` handler to level mode too, so while the line is low the
ISR fires continuously until the edge trigger is restored. Disable the pin's
interrupt (`gpio_intr_disable`) before arming the wake and re-enable it after
setting `GPIO_INTR_NEGEDGE` again.

## Sleeping right after an OTA rolled the update back

The new image confirms itself after a 10 s health check. A long press within
those 10 s went into deep sleep with the image still `PENDING_VERIFY`; the
wake is a reboot through the bootloader, which counts it as a failed boot
and rolls back. `goDeepSleep()` now confirms a pending image first.

## A minimal view never light-slept below 15 % brightness

`minimalCycle()` waited for the fade to reach `BRIGHTNESS_MINIMAL` before
light-sleeping, but `brightnessFor()` caps every level at the user's
brightness. With the quick-settings brightness under 15 % the panel never got
there, so the CPU stayed awake for the whole minimal view. It now compares
against the capped level that the fade actually targets.

## Simulator taps move with the pixel shift

The app manager shifts every screen by up to 4 px every 3 minutes (burn-in).
A simulator flow that runs long enough (the Timer's 75 s stopwatch and 51 s
countdown) shifts the menu, and a tap at a fixed x on the brightness bar read
37 % instead of 38 %. Tests that convert a tap position into a value add the
current root offset.

## `SOUND_VOLUME` 0 was the loudest setting

The ported ES8311 code wrote `SOUND_VOLUME * 256 / 100 - 1` to register
`0x32`: 0 wrapped to `0xFF` (+32 dB, the loudest) and 100 also gave `0xFF`,
which clips. Espressif's driver special-cases 0. Now 0 is mute and 100 is
`0xBF` (0 dB). (The volume is now a quick setting, `settings::volumePct()`,
with the same rule: 0 % writes `0x00`, never more than `0xBF`.)

## Every `lv_obj_set_style_*` call redraws the object

Style setters invalidate the object even when the value is unchanged
(`lv_obj_refresh_style`). Code that runs every loop pass or every scroll
event (OTA screen, menu focus fade) must compare first and set only on
change. `lv_obj_set_hidden()` and `lv_arc_set_value()` already skip
unchanged values; labels go through `ui::setText()`.

## Faint lines across a shape filled as a triangle fan

LVGL anti-aliases every triangle edge, so a polygon filled as a fan of many
thin triangles shows faint seams between them (the horizon's ground did).
Fill with as few triangles as possible: the ground is one big triangle over
the whole half-plane, and a wide black `lv_draw_arc` ring masks everything
outside the disc. Note `lv_draw_arc_dsc_t.radius` is the *outer* radius;
the stroke grows inward by `width`.

## Taps on a button do nothing when they hit its child

`lv_obj_create()` children are clickable by default, so a tap on a
decorative child (the die icon in a strip button) is taken by the child and
never reaches the button's `CLICKED` handler. Labels aren't clickable, which
is why it only shows up with plain objects. The simulator flow test caught
it. `ui::box()` is now created non-clickable (the Espresso history strip had
the same problem); widgets that take taps opt in with
`lv_obj_set_clickable(obj, true)`. The menu's list stays an `lv_obj_create()`
so a drag that starts between the discs still scrolls it.

## A dodecahedron "face direction" that is really a vertex

For the dodecahedron with vertices (±1, ±1, ±1), (0, ±1/φ, ±φ), …, the
direction (0, 1, φ) points at a *vertex*; the face centres are along
(0, φ, 1) and its cyclic permutations. `test_dice_core` checks that the
rest pose puts a whole face (5 vertices at one depth) toward the viewer.

## OTA progress screen freezes during the upload

`ArduinoOTA.handle()` receives the whole image inside one call, so the main
loop (and LVGL) stops for the duration. `core::ota::setProgressHook()` runs
the app tick + `lv_timer_handler()` from the progress callback, rate-limited
to 100 ms.

## A label refresh redraws even when the text is the same

`lv_label_set_text()` always reallocates and invalidates. Periodic status
updates use `ui::setText()` / `ui::setTextFmt()`, which skip identical text.
The same goes for style setters: `lv_obj_set_style_*()` invalidates even when
the value doesn't change, so per-frame code compares first.

## A full-screen `lv_line` redraws the whole screen every frame

An `lv_line` sized to the screen (points in screen coordinates) invalidates
368 × 448 px whenever its points change. Keep the object at the curve's
bounding box and make the points relative to it (bot face mouth).

## Deleting a widget inside its own event handler crashes

LVGL is still using the object after the callback returns. Set a flag in the
handler and rebuild the view on the next tick (System app → update screen).

## Two `lv_anim`s on the same variable cancel each other

`lv_anim_start()` replaces a running animation with the same `var` and
`exec_cb`. The power-off/on animation uses separate dummy variables as tags
per phase; `early_apply = false` stops a delayed phase from jumping its start
value in immediately.

## LVGL 9.6 deprecations

`lv_obj_add_flag/remove_flag` for hidden/clickable/scrollable… are deprecated
in favour of setters (`lv_obj_set_hidden()`, `lv_obj_set_clickable()`, …), and
`LV_COLOR_DEPTH` in `lv_conf.h` is replaced by `LV_COLOR_FORMAT_DEFAULT`.
The build is kept at 0 warnings, so use the new names.

## New OTA image confirmed too early

The Arduino core marks a pending OTA image valid during startup unless the
weak `verifyRollbackLater()` returns true. It's a C symbol: define it as
`extern "C" bool verifyRollbackLater()`, or the override silently doesn't
link and rollback never happens.

## XPowersLib `setIrqLevel()` sets the wrong bits

In 0.3.3 the long-press time helper for the AXP2101 masks the wrong bits of
register `0x27`. We write bits 5:4 of `0x27` directly (`00` = 1 s long press).

## Vendor ES8311 driver can't be used with `Wire`

Espressif's `es8311` component uses the legacy I2C driver; Arduino core 3.x's
`Wire` uses the new `i2c_master` driver, and the two can't coexist in one
firmware (abort at boot). The register sequence is ported onto our I2C bus.

## Native env inherited the ESP32 board

Settings in a bare `[env]` section apply to every environment, including
`[env:native]`. Shared device settings live in `[esp32]` and device envs
`extends = esp32`.

## GitHub can't render partitions.csv

GitHub previews `.csv` files as tables and treats the first line as the header.
A leading `#` comment containing commas gave it the wrong column count. Keep
the `# Name, Type, SubType, Offset, Size, Flags` header as the first line and
put explanations in docs, not in comment lines with commas.

## Display/touch stay dead after a soft reset

An ESP32 restart does not reset the AMOLED or touch controller — their reset
lines hang off the TCA9554 IO expander, not a GPIO. Pulse EXIO0–2
LOW → 20 ms → HIGH before initialising them (vendor examples do the same).

## No serial output over USB-C

`Serial` only goes to the USB-C port with `ARDUINO_USB_CDC_ON_BOOT=1` (set in
`platformio.ini`). Output printed before the host opens the port is lost, so
`setup()` waits up to `SERIAL_WAIT_MS` for a monitor.

## Board doesn't enumerate / upload fails

If the firmware crashes early or sleeps, the native USB port can vanish. Hold
BOOT, replug USB, release BOOT → ROM download mode, then upload.

## V2 image is shifted by 16 px

The CO5300 (V2) panel sits 16 columns into the driver RAM: every CASET
window is shifted by 16 (`core::board::lcdColumnOffset()`, passed to
`core::display::begin()`). V1 (SH8601) has no offset.

## Taps near the bezel and between keys go nowhere

Close to the screen edge a finger is partly off the glass, so the panel
reports it less reliably, and layouts keep a `kPad` margin there. Small
targets with gaps and margins (the Wi-Fi keypad) feel unresponsive at the
edges. Route the whole area to the nearest target with one transparent pad
(see `WifiApp::onPad`) instead of making each key its own click target. The
debug build logs every touch-down position (`touch: down x,y`) so you can
check how the panel behaves at the edges.
