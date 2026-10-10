# Apps

One spec per app. The system they plug into (app interface, power policies,
menu, burn-in rules) is in [../ARCHITECTURE.md](../ARCHITECTURE.md).

| App | id | Power policy | Network | Spec |
| --- | --- | --- | --- | --- |
| Home Assistant lights | `home_assistant` | Normal | Wi-Fi | [home-assistant.md](home-assistant.md) |
| Bot face | `bot_face` | Normal | — | [bot-face.md](bot-face.md) |
| Pomodoro | `pomodoro` | LowPowerDisplay while running | — | [pomodoro.md](pomodoro.md) |
| Espresso shot timer | `espresso` | KeepAwake while running | — | [espresso-timer.md](espresso-timer.md) |
| Dice | `dice` | Normal | — | [dice.md](dice.md) |
| Level | `level` | Normal (dims after 60 s) | — | [level.md](level.md) |
| System (info + OTA) | `system` | Normal; KeepAwake on the update screen | Wi-Fi | [system.md](system.md) |

The menu is built for 10+ apps; candidates for later: clock / watch face,
settings, battery & system info, stopwatch / countdown, coin flip, weather from Home Assistant, HA scenes.

## Adding an app

1. Copy the template below into `docs/apps/<name>.md` and fill it in (agree
   on it before coding).
2. Create `src/apps/<name>/` with a class implementing `App`
   (`src/apps/app.h`).
3. Draw a round icon (~130 px, transparent background, works on black) and
   convert it with the LVGL image converter into `src/ui/icons/`.
4. Register the app in `main.cpp`; that's its position in the menu.
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
Policy, when it changes, what minimal mode looks like (if any).

## Persistence
What survives deep sleep / power-off.

## Config
`config.h` keys with defaults.

## Errors
What the user sees when things fail.

## Acceptance (on device)
Checklist.

## Later
Ideas not in the first version.
```
