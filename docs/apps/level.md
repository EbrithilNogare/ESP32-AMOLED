# Level

## Purpose

A spirit level and inclinometer using the onboard QMI8658 accelerometer.
Lying flat it levels a surface on both axes; standing on an edge it shows
level or plumb plus how far the screen leans up or down. Approved design:
[../design/level.html](../design/level.html) (design A for flat, design B
with lean for edge, chosen from five drafts).

## Screens

```
 Lying flat (Bullseye)          On an edge (Horizon)
┌──────────────────────────┐   ┌──────────────────────────┐
│ SURFACE           ZEROED │   │ EDGE · LEVEL             │
│ X +2.4°          −1.1° Y │   │ rotation 12.0°  ╷ roll    │
│        ╭──────╮          │   │      ╭── scale ──╮       │
│      ╭─┤  ◎   ├─╮        │   │     │ ─── 10 ─── │       │
│      │ │ ●    │ │        │   │     │ ═══╦╤╦═══  │ sky   │
│      ╰─┤      ├─╯        │   │     │╱ horizon ╱ │ ground│
│        ╰──────╯          │   │      ╰───────────╯       │
│          2.6°            │   │ 12.0°          4.0°      │
│                          │   │ LEVEL        LEAN UP     │
│ [Zero] [Hold]   [° | %]  │   │ [Zero] [Hold]   [° | %]  │
└──────────────────────────┘   └──────────────────────────┘
```

Standing on its **left edge** the whole screen turns to landscape
(448 × 368): the horizon disc on the left, the readouts and Zero / Hold /
° % stacked in a column on the right.

```
 Landscape (standing on the left edge)
┌────────────────────────────────────┐
│ EDGE · LEVEL                ZEROED │
│      ╭── scale ──╮     rotation 1.5°│
│     │ ─── 10 ─── │             1.5° │
│     │ ═══╦╤╦═══  │            LEVEL │
│     │╱ horizon ╱ │             2.0° │
│     │            │          LEAN UP │
│      ╰───────────╯         [ Zero ] │
│                            [ Hold ] │
│                            [ ° | % ]│
└────────────────────────────────────┘
```

Simulator frames: `docs/design/screens/22-level-flat.png`,
`23-level-flat-level.png`, `24-level-edge.png`, `50-level-landscape.png`
(the panel's view, so it shows sideways).

- **Flat — bullseye vial.** The bubble moves toward the high side through
  rings at 1°, 3° and 6° (distance = R · tanh(tilt / 7), so it is precise
  near level and never leaves the vial). X (left / right) and Y (front /
  back) at the top, the total tilt under the vial.
- **Edge — attitude horizon.** The horizon rotates with the board
  (roll scale and pointer on top; the bottom-left readout is the deviation
  from the nearest level or plumb reference) and slides along its normal with
  the lean (5 px per degree, pitch ladder every 5°, labels every 10°; the
  bottom-right readout says LEAN UP / LEAN DOWN). The fixed aircraft mark is
  green only when both rotation and lean are level.
- **Colours:** green within 0.3°, the accent `LEVEL_COLOR` (yellow) within
  2°, white beyond.
- **Bottom bar:** Zero (measure relative to the current surface; tap again to
  reset), Hold (freeze the reading and the view), ° / % (percent slope =
  tan × 100).

## Behaviour

- The view switches by itself: flat while the screen is less than 40° from
  horizontal, edge above 50° (hysteresis in between). Face down counts as flat.
- Rotation 0 = standing on the bottom edge; the nearest of 0°, ±90° (plumb)
  and 180° is the reference. It switches only once another reference is
  closer by 2°, so the readout doesn't flicker between level and plumb
  around 45°.
- **Landscape:** only on the left edge (the right edge carries the buttons,
  so the board doesn't stand on it). Entered within 35° of it, left beyond
  50° or when lying down (`level::landscapeFor`). Turned, rotation is
  measured from the left edge, so "level" means the new bottom is level.
  Hold keeps the orientation too. Back to the menu always turns it back.
- Zero is kept per view in NVS (`level`), with the unit.
- A short beep (`LEVEL_BEEP`) when the reading becomes level, at most every
  1.5 s, with a little hysteresis.
- Idle dim after `LEVEL_IDLE_DIM_MS` (60 s) instead of the usual 30 s.
- The IMU is powered only while Level is on screen: off when the app closes
  or the screen goes off (`onScreen()`), back on (fresh filter) when it
  returns. A sleep reopens Level.

## Sensor

- `src/core/imu.cpp`: own QMI8658 driver on the shared I2C bus (register
  values checked against SensorLib): accelerometer only, ±2 g, 112 Hz with
  the on-chip low-pass filter. It is powered down (oscillator off too)
  unless Level is on screen.
- Sampled every `LEVEL_SAMPLE_MS` (20 ms) and smoothed (exponential, α = 0.2,
  in `lib/LevelCore`); the screen redraws at most 30 times a second and only
  when a value changes by 0.05° or more.
- The gyroscope isn't used yet. If the bubble is too jumpy or too slow when
  the board moves, a complementary filter with the gyro is the next step.

## Config (`include/config.h`)

| Macro | Default | Meaning |
| --- | --- | --- |
| `LEVEL_COLOR` | `0xFFD54F` | accent: menu icon, near-level colour, horizon line, Hold / Zero |
| `LEVEL_IDLE_DIM_MS` | 60000 | dim delay while the app is open |
| `LEVEL_BEEP` | 1 | beep when level |
| `LEVEL_SAMPLE_MS` | 20 | accelerometer read period |
| `IMU_SCREEN_{X,Y,Z}_{AXIS,SIGN}` | identity | sensor → screen axis mapping |

## Implementation

- `lib/LevelCore`: angles from the gravity vector (flat X / Y, edge rotation /
  lean), view choice with hysteresis, nearest level / plumb reference,
  percent slope, bubble mapping, smoothing. Host-tested
  (`test/test_level_core`).
- `src/apps/level/level_app.cpp`: draws in the stage's `LV_EVENT_DRAW_MAIN`
  via `src/ui/draw.h`. The ground of the horizon is one large triangle over
  the half-plane, then a wide black ring masks everything outside the disc
  (a fan of many triangles showed anti-aliasing seams).

## On device (checklist)

- [ ] **Axis mapping first:** lying face up the flat view shows ~0° / ~0°;
      lifting the **left** edge makes X positive (right side low) and moves
      the bubble left; lifting the **top** edge makes Y positive (bottom side
      low) and moves the bubble up. Standing on the bottom edge reads rotation ~0°; turning the
      board clockwise makes the horizon turn counter-clockwise; tipping the
      screen toward the sky reads LEAN UP. If an axis is swapped or inverted,
      fix `IMU_SCREEN_*` in `config.h` and note the result in HARDWARE.md.
- [ ] Reading on a known-level surface is within ±0.3° (else Zero handles
      the mounting offset; record the offset in HARDWARE.md)
- [ ] The bubble glides without visible jitter when the board rests
- [ ] The view switches flat ↔ edge cleanly around 45° without flicker
- [ ] Zero, Hold and ° / % behave as described; Zero survives deep sleep
- [ ] The beep sounds once when it becomes level
