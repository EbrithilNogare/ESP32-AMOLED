# Dice

## Purpose

A tabletop (D&D) dice roller: all standard dice, several at once (6d6), and
rolls with advantage or disadvantage. The dice are 3D wireframe polyhedra
that spin and land exactly face-on, with the result on the front face.
Approved design: option D of the five drafts, revised in
[../design/dice-wireframe.html](../design/dice-wireframe.html) (orange
colour from `config.h`, green Adv, red Dis, exact face-on landing).

## Screens

```
 Idle (d20, Flat)               Spinning                       Landed (d20, Adv)
┌──────────────────────────┐   ┌──────────────────────────┐   ┌──────────────────────────┐
│ ◬ ▢ ⬡ ◇ ⬠ ⬡ ◇◇           │   │ ◬ ▢ ⬡ ◇ ⬠ [⬡] ◇◇         │   │ ◬ ▢ ⬡ ◇ ⬠ [⬡] ◇◇         │
│ d4 … [d20] d100          │   │                          │   │   KEPT        DROPPED    │
│        /\                │   │      (wireframe          │   │   /19\         /18\      │
│       / ? \              │   │       tumbling)          │   │   ‾‾‾‾          ‾‾‾‾     │
│       ‾‾‾‾‾              │   │                          │   │                          │
│      Tap to roll         │   │                          │   │ Advantage · higher kept  │
│ [− 1d20 +] [Flat|Adv|Dis]│   │ [− 1d20 +] [Flat|Adv|Dis]│   │ [− 1d20 +] [Flat|ADV|Dis]│
└──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘
```

Simulator frames: `docs/design/screens/16-dice-idle.png` … `21-dice-d100.png`.

- **Die strip** (top): d4, d6, d8, d10, d12, d20, d100 as flat icons; the
  selected one in the primary colour on a 15 % tint.
- **Stage** (middle): tap anywhere to roll. One die is drawn large; several
  dice share a grid. With Adv/Dis both throws sit side by side, labelled
  1ST / 2ND while spinning and KEPT / DROPPED after landing; the dropped
  throw turns grey.
- **Bottom bar**: count stepper (1–10 dice, 1–4 for d100) and the mode
  switch. Flat uses the primary colour, **Adv is green**, **Dis is red**.
- Totals: with several dice (and for d100, whose two dice add up), `TOTAL` and the sum (or both sums with Adv/Dis,
  the dropped one struck through).
- A single d20 landing on 20 or 1 turns green / red with "NATURAL 20" /
  "NATURAL 1".

## Rules

- Every die is uniform 1..N from `esp_random()` (hardware RNG), with
  rejection sampling so no face is favoured.
- **Advantage** throws the whole set twice and keeps the higher total;
  **disadvantage** keeps the lower; a tie keeps the first throw. On a single
  d20 this is the 5e rule.
- **d100** is thrown as percentile dice: a tens d10 (00–90) and a ones d10
  (0–9); 00 + 0 = 100.

## Animation

- Each die spins about a random axis by 2.2–3.8 turns, easing out (quartic)
  over `DICE_ROLL_MS` + up to 300 ms; dice start 80 ms apart.
- The orientation is `rest × rotation(axis, turns × (1 − ease(p)))`. At the
  end it is exactly the rest pose: one face points straight at the viewer,
  upright (a vertex up on triangles and the pentagon, an edge up on the
  cube, the apex up on the d10 kite). It never stops a few degrees off.
- On landing: a small scale "pop", then the front face fills (15 % tint)
  and shows the number.
- Edges are drawn back to front; back edges thin and faint, front edges
  thicker and brighter.

## Config (`include/config.h`)

| Macro | Default | Meaning |
| --- | --- | --- |
| `DICE_COLOR` | `0xFF8A1F` | primary colour (wireframes, kept throw, selection, Flat) |
| `DICE_ROLL_MS` | 1150 | spin time of one die |
| `DICE_FPS` | 30 | frame rate while spinning |

The last die, count and mode are kept in NVS (`dice`).

## Power

Policy `Normal`; spins are short. Only the stage redraws while dice spin,
at `DICE_FPS`; nothing redraws at rest.

## Implementation

- `lib/DiceCore`: rolls, percentile split, solids (vertices, edges, rest
  pose, front face), spin pose, projection, the flat icons and the grid
  layout. Host-tested (`test/test_dice_core`).
- `src/apps/dice/dice_app.cpp`: draws everything in the stage's
  `LV_EVENT_DRAW_MAIN` with `lv_draw_line`, `lv_draw_triangle` (the face,
  as a fan) and `lv_draw_label`. No canvas buffer. The face numbers use
  Roboto Mono at 24/32/44/56/80 px.

## On device (checklist)

- [ ] Spins look smooth (≥ 25 fps) with 1 die and with 2 × 10 dice
- [ ] Every die type lands visibly face-on and upright
- [ ] Taps on the strip, stepper and mode switch don't start a roll
- [ ] Die, count and mode are remembered after deep sleep
