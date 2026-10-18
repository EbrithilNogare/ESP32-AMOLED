# Hephy

## Purpose

A virtual pet that asks for little: Hephy, a small forge spirit named after
Hephaestus, the smith of the gods. He wears the Bot face's face (same
shapes and motion, `lib/BotFace`) in ember orange. In the menu he sits
after 2048, away from Pomodoro, whose tomato is nearly the same colour. He forges trinkets for
your shelf while you're away.

It's built for being opened a few times a week, not every day:

- **Nothing dies and nothing is lost.** His energy and joy sink slowly while
  you're away, but only to a floor. Neglect makes him sleepy and a little
  lonely, never sick or gone.
- **Every visit pays.** Coming back after a while gives a greeting, a joy
  boost, and usually a finished trinket to collect.
- **The long loop is real time.** A forge takes 1–12 hours of wall clock,
  so you start one and come back later.
- **The short loop is under a minute:** feed him coal, play a round of
  Strike (a 5-tap timing game) that speeds up the forge, and pet him.

## Screens

```
 Main                           Strike                         Shelf
┌──────────────────────────┐  ┌──────────────────────────┐  ┌──────────────────────────┐
│ Hephy  [Lv 3]       [▤]  │  │ Strike            2 / 5  │  │ Shelf             7 / 24 │
│                          │  │                          │  │ [⚿][⚓][⚙][🔔][⌛][💡] │
│     ▄▄▄▄     ▄▄▄▄        │  │     ▄▄▄▄     ▄▄▄▄        │  │ [♨][🔧][🪙][⚡][🛡][⚔]  │
│     ████     ████        │  │     ▀▀▀▀     ▀▀▀▀        │  │ [♞][?] [?] [?] [?] [?]  │
│        ╰────╯            │  │        ────    ✦ ✦       │  │ [?] [?] [?] [?] [?] [?] │
│ Forging a key · 2 h 10 m │  │        Perfect!          │  │                          │
│ 🔥 ▬▬▬▬▬▬▬   ♥ ▬▬▬▬▬     │  │ ▕░░░░▓▓▓▓░░░│░░░░░░░▏    │  │   Crown · Epic           │
│  (🔥)     (🔨)     (⚒)   │  │     Tap to strike        │  │   Forged 12 · Lv 3       │
└──────────────────────────┘  └──────────────────────────┘  └──────────────────────────┘
```

Simulator frames: `docs/design/screens/55-hephy-main.png` … `61-hephy-shelf.png`
(main, coal, forging, Strike, asleep, reveal, shelf).

**Main**
- Header: the name, a level chip, and a shelf button.
- The face, at 72 % of the Bot face's size, centred at y ≈ 180. It wanders
  slowly (lively mode, as in Bot face).
- A status line (body font, dim) for whatever matters most right now:
  "Your crown is ready!", "Forging a key · 2 h 10 m", "Hungry for coal",
  "Missed you!", "Zzz…", or "Ready to forge".
- Two thin meters: energy (flame, orange) and joy (heart, pink).
- Three round buttons: **Coal** (feed), **Strike** (play), **Forge**.
  - Forge is dimmed while a forge runs or energy is under 25.
  - Strike is dimmed under 10 energy.
  - A dimmed button still answers with a reason ("Too tired. Feed me
    coal") instead of nothing.

**Strike**
- The face looks down at the anvil.
- A bar with a hot zone and a marker that sweeps back and forth, getting
  faster each strike (1400 → 900 ms per sweep). Tap anywhere to strike.
- Sparks fly on a hit. The result shows: "Perfect!", "Good" or "Miss".
- After 5 strikes, the round's result shows for 1.5 s: "3 perfect · +14 xp ·
  forge −30 min".

**Shelf**
- 24 slots, 6 × 4. An owned trinket shows its icon in its rarity colour; a
  missing one shows a dim "?".
- Tapping a trinket shows its name and rarity.
- Below the grid: how many he has forged in total, and his level.

**Reveal** (overlay, when a forged trinket is collected)
- Its icon, large, in its rarity colour with a glow, then its name.
- "New · Epic", or "Duplicate · +30 xp".
- "Tap to keep" closes it.

## Rules (`lib/PetCore`, pure and tested)

| | |
| --- | --- |
| Energy | 0–100. Sinks 85 points in 72 h (≈ 1.2/h), down to a floor of 15. Coal +25 (refused at ≥ 90: he shakes his head). Strike −5, Forge −25. |
| Joy | 0–100. Sinks 80 points in 48 h, down to a floor of 20. A visit after ≥ 4 h: +15 and +3 xp. Pet (tap the face): +2. Strike: +4 per perfect, +2 per good. |
| Greeting | Away > 1 h: "Hi again". > 12 h: "Missed you!". > 24 h: he's asleep (eyes closed, floating "z"s) until you tap him. Then he startles awake, happy. |
| Mood (face) | Forging → focused (eyes narrowed, looking down). Energy < 30 → sleepy. Joy < 35 → lonely (flat mouth, looking down). Joy ≥ 75 and energy ≥ 50 → happy (big smile). Otherwise content. |
| Forge | One at a time; needs energy ≥ 25 and no trinket waiting. Rarity odds depend on level (below). He prefers a trinket you don't have yet. Time: common 1 h, rare 3 h, epic 6 h, legendary 12 h. Each perfect strike during a forge takes 10 min off. |
| Odds (common / rare / epic / legendary) | Lv 1–2: 75/22/3/0. Lv 3–5: 60/28/10/2. Lv 6+: 45/32/17/6. |
| Collect | A new trinket: +10/25/60/150 xp by rarity. A duplicate: half that, and the forge count goes up. |
| Level | Level n starts at 25·n(n−1)/2 xp (Lv 2 at 25, Lv 3 at 75, Lv 4 at 150 …), up to Lv 20. |
| Clock | Wall clock (PCF85063 RTC, kept through deep sleep and power-off). If the clock jumps backwards (the RTC lost power), every stored time moves back with it: no decay, the forge time left kept. |

**Trinkets:**
- **Common:** key, anchor, cog, bell, hourglass, lamp, kettle, wrench, coin,
  bolt.
- **Rare:** shield, crossed swords, knight, compass, pocket watch, rocket,
  castle.
- **Epic:** diamond, crown, medal, trophy, star wand.
- **Legendary:** golden owl (Hephaestus built golden automatons), sun spark.

The icons are Material Symbols glyphs. The rarity colours are grey `#B0B0B0`,
blue `#4DA3FF`, purple `#B388FF` and gold `#FFC93C`.

## Interaction

| Input | Effect |
| --- | --- |
| Tap the face | pet: happy hop, +2 joy; wakes him if asleep |
| Coal | coal flies into his mouth, he chews (+25 energy), or he shakes his head (full) |
| Strike | Strike round (5 taps) |
| Forge | starts a forge (sparks while the app is open) |
| A finished forge | when the app opens (or the forge ends while it's open): the reveal |
| Shelf button | Shelf |
| Back | Strike/Shelf → Main; Main → menu |

## Power

- **Normal**, dimming after `HEPHY_IDLE_DIM_MS` (60 s) like Bot face. The
  face wanders, so nothing sits still on the AMOLED.
- **Screen off:** nothing (`sleepNeed` empty), so the device deep-sleeps. He
  "lives" on the wall clock, not on wakes: everything is computed from the
  elapsed time when the app opens. No alarm wakes for a finished forge
  either, because a pet shouldn't light the screen at 3 am.

## Persistence

| What | Where |
| --- | --- |
| `pet::State` (meters, xp, shelf, the forge, times) | NVS `hephy`: written on every action (coal, forge, a round, a collected trinket), on close and at a power-off |
| the same, with what changed since (petting, when he was last seen) | parked (RTC) on every sleep |

It survives sleep, power-off, reset and firmware updates. NVS is written a
few times per visit at most, never at every sleep (ARCHITECTURE §8.5).

## Config

```c
#define HEPHY_COLOR 0xFF5E3A      // the face, menu accent: ember
#define HEPHY_JOY_COLOR 0xFF6FA8  // joy meter
#define HEPHY_IDLE_DIM_MS 60000   // a face: it stays bright a minute, like Bot face
#define HEPHY_FPS 30              // face, sparks (dimmed: BOT_FACE_FPS_DIM)
```

## Errors

- The clock is not valid yet (never set, RTC lost power): time still counts
  from the ESP clock. A backward jump is absorbed as described in the
  Clock rule.
- A corrupt or old NVS blob (size or version mismatch): a fresh Hephy.

## Acceptance (on device)

- [ ] First open: a fresh Hephy, "Ready to forge". Coal, Strike and Forge work.
- [ ] Start a forge, power off for over an hour, power on: the reveal shows the trinket.
- [ ] Away > 24 h (or set the clock forward): he's asleep, and a tap wakes him with "Missed you!".
- [ ] A week away: energy and joy are at their floors, nothing worse.
- [ ] Shelf shows the collection; tapping a trinket names it.

## Later

- Accessories unlocked by level (goggles, a little hammer) drawn on the face.
- React to the IMU (dizzy when shaken), as on the Bot face roadmap.
- Hephy visits the Bot face app when a forge finishes.
