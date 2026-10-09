# Bot face

## Purpose

A simple, friendly animated face: a desk companion.

## Screens

```
┌──────────────────────────┐
│                          │
│     ████        ████     │   two rounded-rectangle eyes
│     ████        ████     │   (colour: accent, e.g. cyan or warm orange)
│     ████        ████     │
│                          │
│         ╲______╱         │   mouth: arc, shape changes with expression
│                          │
└──────────────────────────┘   black background
```

Drawn from LVGL primitives (rounded rects + arcs/lines), so everything
animates by tweening a handful of parameters, not by playing frames:

```cpp
struct FaceParams {
  float eyeOpen;        // 0 closed … 1 open
  float lookX, lookY;   // pupil/eye offset, -1 … 1
  float eyeTilt;        // angry/sad brows
  float mouthCurve;     // -1 frown … 0 flat … 1 smile
  float mouthOpen;      // 0 … 1 (surprised "o")
  float bounceY;        // whole-face vertical offset
};
```

## Behaviour

| Animation | When |
| --- | --- |
| Blink | random every 2–6 s, occasionally a double blink |
| Look around | eyes glance to a random point every few seconds, ease in/out |
| Breathe | slow ±3 px vertical bob |
| Happy squint + bounce | on tap |
| Surprised (big eyes, "o" mouth) | on a quick double tap |
| Sleepy (half-closed eyes, slow blink) | when the device enters Dimmed |
| (later) Dizzy | on shake (IMU) |
| (later) Look toward tilt | IMU tilt |

Expressions are tweens between `FaceParams` presets over 150–400 ms.

## Interaction

| Input | Effect |
| --- | --- |
| Tap | happy reaction |
| Double tap | surprised |
| Back / Home | back to the menu |

## Power

`Normal`: on battery it follows dim → black → sleep. The animation runs at
~30 fps while active and drops to ~10 fps while dimmed.

On the charger it follows the charger timeline (dim at 60 s, plain black at
5 min). The eyes and mouth keep moving, so the face itself doesn't burn in.

## Persistence

None needed.

## Config

```cpp
// ---- bot face ----
#define BOT_FACE_COLOR       0x00E5FF   // eye/mouth colour (RGB888)
#define BOT_FACE_FPS         30
```

## Acceptance (on device)

- [ ] Smooth blinking and looking around, no tearing
- [ ] Tap / double tap reactions
- [ ] On the charger: dims at 60 s, goes black at 5 min, touch restores it
- [ ] On battery the device still sleeps after the idle timeout

## Later

IMU reactions, moods tied to battery level or time of day, the face
reacting to Home Assistant events.
