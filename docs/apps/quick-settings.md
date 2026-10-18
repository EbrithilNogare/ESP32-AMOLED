# Quick settings

## Purpose

An Android-style shade on the menu with the settings worth changing on the
device: which Wi-Fi network to use, how bright the screen is, and how loud
the speaker is (or muted). All are saved on the device (NVS) and survive
reboots and firmware updates.

## Screens

```
 Menu: pull the status bar down        Shade open
┌──────────────────────────┐           ┌──────────────────────────┐
│ 10:35          ⌔ ▮ 82 %  │ ↓ drag    │ 10:35                82 %│
│   ◯ Lights   ◯ Bot face  │           │ ┌──────────────────────┐ │
│                          │           │ │ ⌔             HomeNet│ │  → Wi-Fi screen
│   ◯ Pomodoro ◯ Espresso  │           │ └──────────────────────┘ │
│                          │           │ ┌──────────────────────┐ │
│                          │           │ │☀██████████       60 %│ │  drag: brightness
│                          │           │ └──────────────────────┘ │
│                          │           │ ┌──┐ ┌─────────────────┐ │
│                          │           │ │🔊│ │█████████   80 %│ │  mute | drag: volume
│                          │           │ └──┘ └─────────────────┘ │
│                          │           │           ───            │  grip
│                          │           │░░░░ menu, dimmed ░░░░░░░░│
└──────────────────────────┘           └──────────────────────────┘
```

Simulator frames: `docs/design/screens/28-quick-settings.png`,
`43-quick-settings-muted.png`.

- The shade covers the status bar, so it repeats the time and battery.
- **Wi-Fi row:** icon and "Wi-Fi" on the left, the state on the right:
  **On · *network*** (blue) when connected, **Connecting…** (amber) while
  the radio is up but not connected, **Off** (grey, crossed-out icon) when
  the radio is off, **Set up** when no network is known. The radio runs only
  while an app needs it, so Off is the usual state; before 2026-10-18 the
  row showed the network's name even with the radio off. Tap: the
  [Wi-Fi screen](wifi.md) (`48-quick-settings-wifi-off.png`).
- **Brightness bar:** one wide bar filled to the current level, 5–70 %.
- **Volume:** a mute button (speaker icon; red and crossed out while muted)
  and a bar like brightness, 0–100 %. Muted, the bar turns grey and reads
  "Muted".

## Interaction

| Input | Effect |
| --- | --- |
| Drag the status bar down | the shade follows the finger; let go past a third and it opens, else it closes |
| Tap the status bar | opens the shade |
| Drag the shade up | closes it (let go early and it stays open) |
| Tap the dimmed menu below, BOOT or PWR | closes it |
| Tap the Wi-Fi row | closes the shade, opens the Wi-Fi screen |
| Touch / drag the brightness bar | brightness follows the finger live; saved when the finger lifts |
| Touch / drag the volume bar | volume follows the finger live (hear it on playing noise); unmutes; saved on release, then a blip at the new level unless noise plays |
| Tap the mute button | mute ↔ unmute, saved at once |

## Brightness

- The value is the **active** brightness, on battery and on the charger
  alike. The 70 % ceiling keeps the old charger cap's burn-in protection.
- Dimmed is at most half of it (and at most 25 % on battery, 40 % on the
  charger), so dimming always shows. Minimal views keep their own fixed low
  levels (Pomodoro 15 %, Timer 25 %), never above this one.
- Saved once per touch, on release (NVS key `bright`), not while dragging.

## Volume

- 0–100 %, linear in dB on the ES8311 DAC: 100 % = 0 dB, 1 % = −40 dB
  (`SOUND_VOLUME_RANGE_DB`), 0 % = silent. Default 80 % (−8 dB).
- Applies to everything the speaker plays: beeps (Pomodoro, Timer, Level)
  and the [Noise](noise.md) app. A change reaches a playing sound within one
  16 ms buffer.
- **Mute** silences all of it and keeps the speaker amplifier off; beeps are
  dropped, noise keeps "playing" silently, so unmuting brings it back.

## Persistence

NVS keys `bright` (percent), `volume` (percent + muted) and `wifi` (see
[wifi.md](wifi.md)). Firmware
updates over USB or Wi-Fi rewrite only the app partition, so they survive;
"erase flash" clears them.

## Config (`include/config.h`)

| Macro | Default | Meaning |
| --- | --- | --- |
| `BRIGHTNESS_USER_MIN_PCT` | 5 | bar minimum |
| `BRIGHTNESS_USER_MAX_PCT` | 70 | bar maximum |
| `BRIGHTNESS_USER_DEFAULT_PCT` | 60 | until the user sets one |
| `SOUND_VOLUME_DEFAULT_PCT` | 80 | until the user sets one |
| `SOUND_VOLUME_RANGE_DB` | 40 | how quiet 1 % is |

## Implementation

- `src/apps/menu/quick_settings.{h,cpp}`: the shade, part of the menu screen
  (the status bar is its handle). Drag tracking uses the active input
  device's point, so it works whatever is under the finger.
- `src/core/settings.{h,cpp}`: brightness, volume, mute and Wi-Fi in NVS.
- `core::sound::applyVolume()`: the sound task writes the DAC volume and
  switches the amplifier before its next buffer.
- `core::power::applyBrightness()` shows a new value at once (no fade);
  `power` builds its levels from `settings::brightnessLevel()`.

## Acceptance (on device)

- [ ] Dragging the status bar pulls the shade smoothly; a short pull snaps back
- [ ] The brightness follows the finger without lag; the value is still set
      after a reboot and after a wireless update
- [ ] 5 % is still readable indoors; dimming is visible at every level
- [ ] BOOT / PWR / a tap below close the shade
- [ ] Volume: 1 % is just audible, 100 % loud but not distorted; the blip on
      release matches; mute silences beeps and noise without hiss
