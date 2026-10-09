# UI design & assets

> **Status: plan.** The design system all apps share, and where images, fonts
> and sprites live and how they get onto the device. Tokens defined here are
> implemented once in `src/ui/theme.{h,cpp}`. Apps never hard-code colours,
> sizes or durations.

## 1. Canvas

- **368 × 448 px, portrait**, ~1.8", ~330 ppi. A 16 px font is small, so
  text is ≥ 20 px wherever possible.
- **Safe margin 4 px** on every edge (pixel-shift travel, §6). The panel
  corners may be slightly rounded: keep content out of the 24 px corner squares
  (verify the real radius in M1).
- Touch targets **≥ 56 × 56 px**, ≥ 8 px apart (finger on a small screen).
- The menu has a 32 px status bar; apps are full-screen (no status bar) to
  maximise space. Navigation is by the hardware Back/Home buttons, so no
  on-screen back buttons are needed.

## 2. Colour (AMOLED-first)

A black pixel is off: it costs no power and doesn't wear the panel. Backgrounds
are pure black, and colour is spent on content.

| Token | Value | Use |
| --- | --- | --- |
| `bg` | `#000000` | every background |
| `surface` | `#141414` | tiles, cards, pressed states |
| `surface_hi` | `#242424` | raised/selected surface |
| `outline` | `#3A3A3A` | 1–2 px borders, disabled outlines |
| `text` | `#EDEDED` | primary text (not pure white: less glare and wear) |
| `text_dim` | `#9A9A9A` | secondary text, captions |
| `text_off` | `#5A5A5A` | disabled |
| `success` | `#4CD07D` | in target, OK |
| `warning` | `#FFB020` | close to target, stale |
| `error` | `#FF5A5F` | failures, over target |
| `info` | `#4DA3FF` | neutral highlight, "early" |

Per-app **accent** (icon glyph + ring, in-app highlights):

| App | Accent |
| --- | --- |
| Home Assistant lights | `#FFB300` amber |
| Bot face | `#00E5FF` cyan |
| Pomodoro | `#FF5A4E` tomato |
| Espresso | `#C8915A` crema |

New apps pick a hue not already in use. The accent at ~15 % brightness on black
is the "tinted surface" (`accent_dim`, computed by the theme).

## 3. Typography

| Token | Font | Size | Use |
| --- | --- | --- | --- |
| `caption` | Inter Medium | 16 | status bar, small labels |
| `body` | Inter Regular | 20 | normal text, menu labels |
| `title` | Inter SemiBold | 28 | screen titles, tile labels |
| `display` | Inter SemiBold | 48 | big single values |
| `digits_l` | Roboto Mono Medium | 96 | pomodoro full view |
| `digits_xl` | Roboto Mono Medium | 128 | espresso timer |
| `icon_s` | Material Symbols Rounded | 32 | status bar, inline icons |
| `icon_l` | Material Symbols Rounded | 64 | app icons, light tiles |

- All fonts are rendered at **4 bpp** (anti-aliased).
- Text fonts include **Latin-1 + Latin Extended-A** (U+0020–007E,
  U+00A0–017F), so Czech labels (ě š č ř ž ý á í é ů ú) render.
- Digit fonts contain only `0123456789:.- `, which keeps the 128 px font
  small. Icon fonts contain only the codepoints listed in `assets.toml`.

## 4. Spacing, shape, motion

- **Spacing:** 4 px grid: `4, 8, 12, 16, 24, 32`. Default content padding 16.
- **Radius:** tiles 24, cards 16, buttons pill (`LV_RADIUS_CIRCLE`), app
  icons full circle.
- **Motion:**

  | Token | Duration | Easing | Use |
  | --- | --- | --- | --- |
  | `fast` | 120 ms | ease-out | press feedback, small state changes |
  | `normal` | 220 ms | ease-in-out | screen transitions (zoom + fade), tile state |
  | `slow` | 400 ms | ease-in-out | expressions, large moves |
  | `fade_brightness` | 300 ms | linear | dim/brighten |
  | `power_anim` | 1000 ms | custom | power-off (CRT collapse) / power-on |

  Nothing loops faster than needed. Idle screens either stay static or animate
  at ≤ 10 fps (power).
- **Press feedback:** everything tappable scales to 94 % and brightens its
  surface for `fast`, so feedback is immediate even if the action is slow
  (network).

## 5. App icons

Drawn by LVGL, no bitmap needed:

```
      ╭───────────╮         128 px circle
    ╱   accent_dim  ╲       fill: accent at ~15 % (accent_dim)
   │                 │      ring: 2 px accent
   │       💡        │      glyph: icon_l (64 px) in accent,
   │                 │             from the Material Symbols subset
    ╲               ╱
      ╰───────────╯
         Lights              label: body, text_dim, centred, 1 line
```

`AppInfo` gives either an icon-font codepoint (default) or an image (a custom
PNG through the asset pipeline, 128 × 128, RGB565A8) when a glyph doesn't fit.

## 6. Burn-in rules for designers

1. Black backgrounds; no large static bright areas.
2. Static elements stay small and dim (`text_dim`) where possible.
3. The display driver shifts the whole picture by up to ±4 px every few
   minutes, so nothing may touch the screen edge (safe margin).
4. Long-lived views (pomodoro minimal) use thin lines and low brightness, and
   move their content themselves on each refresh.
5. On the charger the screen goes plain black after 5 min idle.

## 7. Assets: where they live and how they're built

```
assets/                       SOURCES — committed, the only place you edit
├── assets.toml               manifest: every asset, its format and destination
├── fonts/                    .ttf/.otf + LICENSE per family
├── images/                   .png (sRGB, exported at 1:1 pixel size)
├── icons/                    custom app icons, 128×128 .png (optional)
├── sprites/<name>/           sheet.png (grid of frames)
└── sounds/                   .wav, 16 kHz mono (later)

tools/
├── assets.py                 builds everything from assets.toml
├── requirements.txt          pypng, lz4 (+ fonttools if instancing is needed)
├── package.json              lv_font_conv 1.5.3
└── lvgl/LVGLImage.py         copied from LVGL v9.6.0 scripts/ (same tag as the firmware)

src/ui/generated/             OUTPUT — committed, never hand-edited
├── assets.h                  one header declaring every font / image / sprite
├── fonts/*.c
├── images/*.c
└── sprites/*.c

data/                         OUTPUT for LittleFS (big or optional assets as LVGL .bin)
```

### Manifest example

```toml
[[font]]
name   = "body"                      # → lv_font_t font_body
file   = "fonts/Inter-Regular.ttf"
size   = 20
bpp    = 4
range  = ["0x20-0x7E", "0xA0-0x17F"]

[[font]]
name    = "digits_xl"
file    = "fonts/RobotoMono-Medium.ttf"
size    = 128
bpp     = 4
symbols = "0123456789:.- "

[[font]]
name    = "icon_l"
file    = "fonts/MaterialSymbolsRounded.ttf"
size    = 64
bpp     = 4
icons   = ["lightbulb", "sentiment_satisfied", "timer", "coffee", "settings"]  # names → codepoints

[[image]]
name   = "ha_bulb_on"
file   = "images/bulb_on.png"
format = "RGB565A8"                  # RGB565 | RGB565A8 | A8 | A4
dest   = "flash"                     # flash (C array) | fs (data/*.bin)

[[sprite]]
name   = "steam"
file   = "sprites/steam/sheet.png"
frame  = [64, 64]                    # w, h
frames = 12                          # read row by row
fps    = 12
loop   = "loop"                      # loop | once | pingpong
format = "RGB565A8"
```

`python tools/assets.py` regenerates everything. Asset changes go in one
commit: source + manifest + generated output. CI later runs the generator
and fails on any diff, so outputs can't drift from sources.

### Choosing a format

| Format | Bytes/px | When |
| --- | --- | --- |
| `RGB565` | 2 | opaque images (full-screen art, photos) |
| `RGB565A8` | 3 | colour images with soft edges/transparency |
| `A8` / `A4` | 1 / 0.5 | single-colour art tinted at runtime (`image_recolor`), e.g. a glyph-like illustration in the app accent |

The display renders in `RGB565_SWAPPED`; LVGL converts while drawing, so
sources stay plain RGB565.

### Flash (C array) vs. filesystem (`.bin` in LittleFS)

| | Flash (default) | LittleFS `data/` |
| --- | --- | --- |
| For | fonts, icons, UI images, sprites ≤ ~150 KB | big or optional: full-screen images (368×448 RGB565 ≈ 330 KB), long animations, sounds |
| Access | instant, memory-mapped | loaded via LVGL's LittleFS driver (`A:/img/x.bin`), optional LZ4, decoded into the PSRAM image cache |
| Update | with the firmware | `pio run -t uploadfs` (VS Code task) |
| Budget | 4 MB app slot (keep the firmware ≤ 3 MB for headroom) | ~8 MB |

### Sprites

- A sheet is a PNG grid of equal frames, read row by row. `assets.py` slices
  it into frames, converts each, and emits:

  ```cpp
  extern const lv_image_dsc_t *const sprite_steam_frames[];
  extern const SpriteInfo sprite_steam;   // {frames, count, fps, w, h, loop}
  ```

- Playback goes through a small `ui::Sprite` helper (an `lv_image` + `lv_anim`
  stepping the source), which supports loop/once/ping-pong, a callback on the
  last frame, and pause when the app pauses. `lv_animimg` is enough for simple
  loops.
- Keep sprites small (e.g. 64×64 × 12 frames RGB565A8 ≈ 147 KB). Anything
  big or long-running is better as **LVGL drawing + tweened parameters**
  (like the bot face): no flash cost, any size, perfectly smooth.

## 8. LVGL rendering setup

| Setting | Value | Why |
| --- | --- | --- |
| Colour format | `RGB565_SWAPPED` | what the QSPI panel takes; no swap pass in the flush |
| Render mode | partial, **2 draw buffers × 368 × 40 lines** (~59 KB) in internal DMA-capable RAM | render the next chunk while DMA sends the last; internal RAM renders faster than PSRAM |
| Rounder | x1/y1 even, x2/y2 odd | CO5300/SH8601 requirement |
| Refresh period | 16 ms active (≈ 60 fps cap), 100 ms when dimmed | power |
| LVGL heap | custom malloc → PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) | widgets/styles off internal RAM |
| Image cache | ~1 MB in PSRAM | decoded LittleFS images |
| OS integration | `LV_OS_NONE`; only the UI task calls `lv_*` | simple and deterministic (ARCHITECTURE §6) |

The draw buffers are a deliberate exception to the "big buffers in PSRAM"
rule in AGENTS.md: they're small and on the hot path.
