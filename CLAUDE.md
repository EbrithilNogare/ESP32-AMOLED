# ESP32-AMOLED

PlatformIO + Arduino (core 3.x via pioarduino) firmware for the Waveshare
ESP32-S3-Touch-AMOLED-1.8.

- Hardware facts (pins, I2C addresses, chips, board revisions, libraries,
  bring-up code): `docs/HARDWARE.md`. Pin constants: `include/board.h` — use
  them, never hard-code GPIO numbers.
- App tunables go in `include/config.h`; credentials in `include/secrets.h`
  (gitignored) with a matching placeholder in `include/secrets.example.h`.
- Builds: `pio run -e amoled` and `pio run -e amoled-debug` must both stay green (CI).
- The display/touch need the TCA9554 reset pulse (EXIO0-2) before init; the
  panel has no backlight pin, brightness is a panel command.
