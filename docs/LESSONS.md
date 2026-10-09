# Lessons learned

Gotchas from real hardware, libraries, and the toolchain. Add one whenever
something cost time to figure out. Newest first.

Format: `## Short symptom` — what happened, the cause, the fix.

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
