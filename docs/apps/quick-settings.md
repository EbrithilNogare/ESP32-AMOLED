# Quick settings

## Purpose

An Android-style shade on the menu with the two settings worth changing on
the device: which Wi-Fi network to use, and how bright the screen is. Both
are saved on the device (NVS) and survive reboots and firmware updates.

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
│                          │           │           ───            │  grip
│                          │           │░░░░ menu, dimmed ░░░░░░░░│
└──────────────────────────┘           └──────────────────────────┘
```

Simulator frame: `docs/design/screens/28-quick-settings.png`.

- The shade covers the status bar, so it repeats the time and battery.
- **Wi-Fi row:** icon on the left (blue while connected, a crossed-out icon
  when no network is set up), the network's name on the right ("Set up
  Wi-Fi" when there is none). Tap: the [Wi-Fi screen](wifi.md).
- **Brightness bar:** one wide bar filled to the current level, 5–70 %.

## Interaction

| Input | Effect |
| --- | --- |
| Drag the status bar down | the shade follows the finger; let go past a third and it opens, else it closes |
| Tap the status bar | opens the shade |
| Drag the shade up | closes it (let go early and it stays open) |
| Tap the dimmed menu below, BOOT or PWR | closes it |
| Tap the Wi-Fi row | closes the shade, opens the Wi-Fi screen |
| Touch / drag the brightness bar | brightness follows the finger live; saved when the finger lifts |

## Brightness

- The value is the **active** brightness, on battery and on the charger
  alike. The 70 % ceiling keeps the old charger cap's burn-in protection.
- Dimmed is at most half of it (and at most 25 % on battery, 40 % on the
  charger), so dimming always shows. The pomodoro minimal view keeps its own
  fixed low level.
- Saved once per touch, on release (NVS key `bright`), not while dragging.

## Persistence

NVS keys `bright` (percent) and `wifi` (see [wifi.md](wifi.md)). Firmware
updates over USB or Wi-Fi rewrite only the app partition, so both survive;
"erase flash" clears them.

## Config (`include/config.h`)

| Macro | Default | Meaning |
| --- | --- | --- |
| `BRIGHTNESS_USER_MIN_PCT` | 5 | bar minimum |
| `BRIGHTNESS_USER_MAX_PCT` | 70 | bar maximum |
| `BRIGHTNESS_USER_DEFAULT_PCT` | 60 | until the user sets one |

## Implementation

- `src/apps/menu/quick_settings.{h,cpp}`: the shade, part of the menu screen
  (the status bar is its handle). Drag tracking uses the active input
  device's point, so it works whatever is under the finger.
- `src/core/settings.{h,cpp}`: brightness and Wi-Fi in NVS.
- `core::power::applyBrightness()` shows a new value at once (no fade);
  `power` builds its levels from `settings::brightnessLevel()`.

## Acceptance (on device)

- [ ] Dragging the status bar pulls the shade smoothly; a short pull snaps back
- [ ] The brightness follows the finger without lag; the value is still set
      after a reboot and after a wireless update
- [ ] 5 % is still readable indoors; dimming is visible at every level
- [ ] BOOT / PWR / a tap below close the shade
