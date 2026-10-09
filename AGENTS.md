# AGENTS.md

Instructions for AI coding agents (Claude Code, Codex, …) working in this repo.
`CLAUDE.md` just imports this file.

## Project

PlatformIO + Arduino (core 3.x via the pioarduino platform) firmware for the
Waveshare ESP32-S3-Touch-AMOLED-1.8 (ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM,
368×448 QSPI AMOLED + touch, AXP2101, QMI8658, PCF85063, ES8311, microSD).

## Read first

| File | What's in it |
| --- | --- |
| `docs/HARDWARE.md` | board spec, V1/V2 revisions, full pinout, I2C map, IO expander, power, libraries, bring-up snippets |
| `docs/ARCHITECTURE.md` | system design: app framework, power/sleep, Home Assistant integration |
| `docs/ROADMAP.md` | milestones with on-device acceptance checklists — find the current one before coding |
| `docs/DECISIONS.md` | why things are the way they are — check before changing toolchain, layout, partitions |
| `docs/LESSONS.md` | gotchas learned on real hardware and in the toolchain |
| `README.md` | layout, build/flash, environments |

## Memory — keep it current

The `docs/` files above are the project's long-term memory between sessions.

- Made a non-obvious choice (library, architecture, partition size, protocol)?
  Add an entry to `docs/DECISIONS.md`.
- Hit a bug or surprise caused by the hardware, a library, or the toolchain?
  Add it to `docs/LESSONS.md` so nobody hits it twice.
- Learned a new hardware fact (pin, address, chip behaviour)? Put it in
  `docs/HARDWARE.md` and, if it's a pin/address, in `include/board.h`.
- Update docs in the same commit as the code they describe.

## Git workflow

- **Normal tasks** (fixes, small features, config, docs): commit directly to
  `main`.
- **Large independent features** (new subsystem, big UI, new protocol): create
  `feature/<short-name>` from `main`, work there, open a PR into `main`.
- Commit messages: imperative summary line, body explaining *why*.
- Never commit `include/secrets.h` or anything with real credentials.

## Code rules

- Use the constants from `include/board.h` — never hard-code GPIO numbers or
  I2C addresses.
- App tunables → `include/config.h`. Credentials → `include/secrets.h`
  (gitignored) + a placeholder with the same name in `include/secrets.example.h`.
- New modules: a header/source pair in `src/`, or a self-contained library in
  `lib/<Name>/`. Keep `main.cpp` as wiring: init in `setup()`, service in `loop()`.
- Third-party libraries go in `platformio.ini` `lib_deps` with a pinned version.
- Large buffers (frame buffers, LVGL draw buffers, audio) belong in PSRAM
  (`ps_malloc` / `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`).

## Build & verify

```bash
pio run -e amoled          # release
pio run -e amoled-debug    # debug
```

Both must build before pushing — CI (`.github/workflows/build.yml`) checks the
same. Flashing needs the physical board; say so instead of claiming hardware
behaviour you couldn't test.

## Hardware must-knows

- Display and touch must be reset through the TCA9554 expander (EXIO0–2
  LOW → 20 ms → HIGH) before init; an ESP restart alone doesn't reset them.
- No backlight pin: AMOLED brightness is panel command `0x51`
  (`gfx->setBrightness()`).
- Default `BOARD_REVISION` is 2 (CO5300 + CST820). V1 boards use SH8601 + FT3168.
