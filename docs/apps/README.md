# Apps

One spec per app. The system they plug into (app interface, power policies,
menu, burn-in rules) is in [../ARCHITECTURE.md](../ARCHITECTURE.md).

| App | id | Power policy (screen on) | Screen off (`sleepNeed`) | After a sleep | Network | Spec |
| --- | --- | --- | --- | --- | --- | --- |
| Home Assistant lights | `home_assistant` | Normal | — | cached states | Wi-Fi | [home-assistant.md](home-assistant.md) |
| Bot face | `bot_face` | Normal (dims after 60 s) | — | reopens | — | [bot-face.md](bot-face.md) |
| Pomodoro | `pomodoro` | LowPowerDisplay while running | wake at the phase end | carries on | — | [pomodoro.md](pomodoro.md) |
| Espresso shot timer | `espresso` | KeepAwake while running | — | the last shot (a running one stops) | — | [espresso-timer.md](espresso-timer.md) |
| Dice | `dice` | Normal | — | the last roll | — | [dice.md](dice.md) |
| Level | `level` | Normal (dims after 60 s) | — (IMU off with the screen) | reopens | — | [level.md](level.md) |
| Timer (clock, stopwatch, countdown) | `timer` | LowPowerDisplay on the clock tab and while a timer runs | wake at the countdown's end; awake while the alarm rings | both timers carry on | — | [timer.md](timer.md) |
| Noise (rain, white, blue) | `noise` | Normal | awake while playing (doze) | paused | — | [noise.md](noise.md) |
| System (info + OTA) | `system` | Normal; KeepAwake on the update screen | — | info view | Wi-Fi | [system.md](system.md) |
| Wi-Fi settings (not in the menu) | `wifi` | Normal (dims after 60 s) | — | menu (not resumable) | Wi-Fi | [wifi.md](wifi.md) |

A power-off pauses every timer and stops the sound; the device then starts
in the menu, with the timers paused where they were.

Part of the menu rather than an app: the [quick settings](quick-settings.md)
shade (Wi-Fi, brightness, volume), pulled down from the status bar.

The menu is built for 10+ apps; candidates for later: coin flip, weather
from Home Assistant, HA scenes.

## Adding an app

1. Copy the template below into `docs/apps/<name>.md` and fill it in (agree
   on it before coding).
2. Create `src/apps/<name>/` with a class implementing `App`
   (`src/apps/app.h`).
3. Pick a Material Symbols glyph for the icon (the menu draws it on a 128 px
   disc in the app's accent colour), add its name to the `icons` list of
   every `icon_*` font in `assets/assets.toml`, run `python tools/assets.py`,
   and use `ICON_<NAME>` in the app's `AppInfo` (see
   [tools/README.md](../../tools/README.md)).
4. Register the app in `main.cpp`; that's its position in the menu.
   Decide what it does with the screen off (ARCHITECTURE §8): settings go
   to NVS in `onWake()` / on change; running state is parked in
   `onSleep()` (`core::store::park()`) and restored in `onWake()`; a
   deadline is a `sleepNeed()` wake time on `core::mono()`, sound is
   `awake`; news for the user is `apps::manager::attention(this)`.
5. App settings go in a `// ---- <app> ----` block in `include/config.h`;
   secrets, if any, in `secrets.example.h`.
6. Add a row to the table above and a milestone to ROADMAP.md.

### Spec template

```markdown
# <App name>

## Purpose
One or two sentences.

## Screens
ASCII sketch(es) of each screen/state.

## Interaction
| Input | Effect |

## States
State machine (mermaid) if the app has one.

## Power
Policy, when it changes, what minimal mode looks like (if any), and what it
needs with the screen off (`sleepNeed()`).

## Persistence
What survives a sleep (parked in RTC memory), a power-off, a reset (NVS).

## Config
`config.h` keys with defaults.

## Errors
What the user sees when things fail.

## Acceptance (on device)
Checklist.

## Later
Ideas not in the first version.
```
