# Lessons learned

Gotchas from real hardware, libraries, and the toolchain. Add one whenever
something cost time to figure out. Newest first.

Format: `## Short symptom` — what happened, the cause, the fix.

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

The CO5300 (V2) panel sits 16 columns into the driver RAM: pass
`LCD_COL_OFFSET` (16) as the column offset to `Arduino_CO5300`. V1 (SH8601)
has no offset.
