# Espresso shot timer

## Purpose

A big, glanceable stopwatch for pulling espresso shots, with colour feedback
on whether the shot ran in the target window, and a short history.

Typical target: a classic shot (about 1:2 ratio) runs ~**25–30 s** from
starting the pump. The window is configurable.

## Screens

```
 Ready                         Running                        Done
┌────────────────────┐        ┌────────────────────┐        ┌────────────────────┐
│ ESPRESSO           │        │                    │        │ last: 27.4 s  ✓    │
│                    │        │                    │        │                    │
│       0.0          │  tap   │      18.6          │  tap   │      27.4          │
│                    │ ─────► │   ▓▓▓▓▓▓▓▓░░░░     │ ─────► │   in target 25–30  │
│   tap to start     │        │   target 25–30 s   │        │                    │
│                    │        │                    │        │ 26.1 ✓ 31.8 ✗ 24.0 │
│ 26.1  31.8  24.0   │        │                    │        │ tap to reset       │
└────────────────────┘        └────────────────────┘        └────────────────────┘
```

- Digits: large (~120 px), tenths of a second, refreshed at ~10 fps while
  running; only the digits and the bar redraw.
- Colour of the digits/bar by elapsed time:

  | Elapsed | Colour | Meaning |
  | --- | --- | --- |
  | < target − 5 s | blue | early |
  | target − 5 s … target min | amber | getting close |
  | target min … max | green | in the window |
  | > max | red | over-extracted territory |

- History: the last 5 shots with ✓/✗, newest first.

## Interaction

| Input | Effect |
| --- | --- |
| Tap anywhere (Ready) | start |
| Tap anywhere (Running) | stop → Done, shot saved to history |
| Tap (Done) | back to Ready |
| Long-press (Done) | discard this shot (not saved) |
| Back (Done) | back to Ready; (Ready/Running) → menu, a running shot keeps timing and is shown on return |
| Home | back to the menu |

Big tap target on purpose: hands are busy or wet.

## States

`Ready → Running → Done → Ready`. Pure logic, unit-testable.

Optional **pre-infusion** (later): a first tap starts a pre-infusion phase,
a second tap marks the main extraction; both times are shown.

## Power

- Running → `KeepAwake`: the screen stays at full brightness, no dimming.
- Ready / Done → `Normal`, with a longer dim delay for this app
  (`ESPRESSO_IDLE_DIM_MS`, 30 s) so the result can be read while handling the
  cup.
- Wi-Fi not used.

## Persistence

The durations of the last 20 shots in NVS (key `shots`); written once per
saved shot.

## Config

```cpp
// ---- espresso ----
#define ESPRESSO_TARGET_MIN_S   25
#define ESPRESSO_TARGET_MAX_S   30
#define ESPRESSO_IDLE_DIM_MS    30000   // overrides the battery dim delay in this app
```

The history size is `shot::kHistory` (20) in
`lib/ShotTimer/src/shot_timer.h`.

## Acceptance (on device)

- [ ] Start/stop by a tap anywhere, readable at arm's length
- [ ] Timing accuracy within ±0.1 s against a phone stopwatch over 30 s
- [ ] Colours switch at the configured thresholds
- [ ] History survives deep sleep and power-off
- [ ] No dimming while running

## Later

- **Auto-start/stop** from pump vibration via the QMI8658 accelerometer
  (device sitting on the machine): detect vibration onset/stop, with a
  threshold calibration screen.
- Dose/yield/ratio entry, a beep at target min/max, stats per day.
