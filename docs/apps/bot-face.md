# Bot face

## Purpose

A simple, friendly animated face: a desk companion that blinks, looks
around, breathes and reacts to touch.

**Reference design:** the bot face in [`docs/design/ui-mockup.html`](../design/ui-mockup.html)
(approved by the owner). The firmware should look and move like it. The
numbers below are taken straight from the mockup (`buildBot()`), so
implementation is a port, not a redesign.

## Look

```
┌──────────────────────────┐  368 × 448, black background
│                          │
│      ▄▄▄▄      ▄▄▄▄      │
│     ██████    ██████     │  two rounded-rectangle eyes
│     ██████    ██████     │  86 × 118 px, corner radius 28
│     ██████    ██████     │  centres x = 124 and 244, y = 196
│      ▀▀▀▀      ▀▀▀▀      │
│                          │
│        ╰────────╯        │  mouth: quadratic curve, 12 px stroke,
│                          │  round caps, 88 px wide, at y = 318
└──────────────────────────┘
```

- One colour for everything: **cyan `#00E5FF`** (the app accent) on black.
  No pupils, brows or outlines: the shapes themselves carry the expression.
- Eyes: filled rounded rectangles. Mouth: an open curve (smile/flat) or, when
  surprised, a filled ellipse ("o").
- All drawing uses LVGL primitives (rounded `lv_obj` rectangles for the eyes,
  an `lv_canvas`/line or arc for the mouth). No bitmaps or sprites.

## Parameters

Everything on screen comes from a small set of parameters. Each frame, every
parameter eases toward its target. Expressions just set new targets.

```cpp
struct FaceParams {
  float open;    // eye openness: 0 closed … 1 normal … 1.25 wide (surprised)
  float lookX;   // gaze, -1 … 1 (left … right)
  float lookY;   // gaze, -0.6 … 0.6 (up … down)
  float curve;   // mouth curvature: 0 flat … 0.55 resting smile … 1 big smile
  float mouthO;  // 0 … 1: "o" mouth (surprised); > 0.15 replaces the curve
  float bounce;  // whole-face vertical offset in px (negative = up)
  float width;   // eye width multiplier: 1 normal … 1.15 surprised
};
```

**Easing:** `p += (target - p) * k` per frame. The mockup uses `k = 0.16` at
60 fps. To be frame-rate independent, use `k = 1 - pow(0.84, dt / 16.7 ms)`
so it looks the same at 30 fps.

### Geometry (screen pixels)

| Element | Formula |
| --- | --- |
| Breathing offset | `breathe = 3 · sin(t / 900 ms)`, so ±3 px with a ~5.7 s period |
| Eye height | `h = max(8, 118 · open · blink)` |
| Eye width | `w = 86 · width` |
| Eye centre | `x = {124, 244} + 18 · lookX`, `y = 196 + 14 · lookY + breathe + bounce` |
| Eye corner radius | `min(28, h / 2)` (so a closed eye becomes a pill) |
| Mouth centre | `x = 184 + 8 · lookX`, `y = 318 + breathe + 0.6 · bounce` |
| Mouth curve | quadratic Bézier from `(x − 44, y)` via `(x, y + 56 · curve)` to `(x + 44, y)`, 12 px stroke, round caps |
| "o" mouth (when `mouthO > 0.15`) | filled ellipse at `(x, y + 6)`, radii `16 + 6 · mouthO` × `24 · mouthO` |

### Blink

`blink` multiplies the eye height for 150 ms:
`blink = 1 − 0.93 · sin(π · phase)`, with phase going 0 → 1. The eyes almost
close (7 %) and reopen.

- Next blink after a random **2–6 s**; **20 %** of blinks are followed by a
  second one 260 ms later (double blink).
- When the device is dimmed, blink intervals are ×1.8 (slower, sleepy).

### Look around

Every **1.4–3.6 s** the gaze target moves: 30 % of the time back to centre,
otherwise to a random point (`lookX` −1…1, `lookY` −0.6…0.6). The easing
makes it a smooth glance, not a jump.

## Expressions (target presets)

| Expression | When | Targets | Held for |
| --- | --- | --- | --- |
| **Resting** | default | open 1, curve 0.55, mouthO 0, bounce 0, width 1 | — |
| **Happy** | tap | open 0.3 (squint), curve 1, width 1.08, bounce −14 → 0 after 180 ms (a hop) | 900 ms |
| **Surprised** | second tap within 320 ms | open 1.25, width 1.15, mouthO 1, curve 0, bounce −6 | 1300 ms |
| **Sleepy** | device in the Dimmed stage | open 0.45, curve 0.25, slower blinks | while dimmed |

After the hold time the targets return to Resting (or Sleepy when dimmed),
and the easing blends back.

## Interaction

| Input | Effect |
| --- | --- |
| Tap | happy reaction |
| Double tap (≤ 320 ms) | surprised |
| Back / Home | back to the menu |

## Power

`Normal`: on battery it follows dim → black → sleep. Animation runs at
~30 fps while active and drops to ~10 fps while dimmed (the sleepy face moves
slowly anyway). On the charger it follows the charger timeline (dim at 60 s,
plain black at 5 min). The eyes keep moving, so the face doesn't burn in.

Only the face's bounding area is redrawn each frame (eyes ~240 × 150 px plus
mouth ~110 × 70 px), not the full screen, which keeps the frame time low.

## Persistence

None needed.

## Config

```cpp
// ---- bot face ----
#define BOT_FACE_COLOR       0x00E5FF   // eye/mouth colour (RGB888)
#define BOT_FACE_FPS         30
#define BOT_FACE_FPS_DIM     10
```

## Implementation notes

- Pure logic (parameters, easing, blink/look scheduling, expression presets)
  lives in `lib/BotFace/` without LVGL, so it can be unit-tested on the PC and
  checked against the mockup's numbers. `src/apps/bot_face/` only maps
  `FaceParams` to LVGL objects.
- Random timing uses `esp_random()` on the device and a seeded generator in
  tests.

## Acceptance (on device)

- [ ] Side by side with the mockup, shapes, proportions and colour match
- [ ] Smooth blinking (incl. occasional double blinks) and glances, no tearing
- [ ] Tap → happy hop; double tap → surprised "o"
- [ ] Sleepy face when dimmed
- [ ] On the charger: dims at 60 s, goes black at 5 min, touch restores it
- [ ] On battery the device still sleeps after the idle timeout

## Later

IMU reactions (dizzy on shake, look toward tilt), moods tied to battery level
or time of day, reacting to Home Assistant events, optional second colour
theme (warm orange).
