# tools/

Developer-machine and CI tools. Nothing here runs on the device.

## Asset pipeline (`assets.py`)

`assets.py` reads `assets/assets.toml` and regenerates every UI asset into
`src/ui/generated/` (fonts, images, sprites and one `assets.h`), plus
`data/img/*.bin` for images that live on LittleFS. The generated C files are
committed, so a normal firmware build (`pio run`) needs neither Node nor these
Python packages. Design background: `docs/UI_DESIGN.md` §7.

### Install (once)

```bash
pip install -r tools/requirements.txt   # Python 3.11+: fonttools, pypng, lz4
npm ci --prefix tools                   # Node >= 18: lv_font_conv 1.5.3 (pinned in package-lock.json)
```

### Run

```bash
python tools/assets.py           # regenerate; writes only files that changed, removes stale ones
python tools/assets.py --check   # CI: rebuild in a temp dir, exit 1 if the committed output differs
python tools/assets.py -v        # more detail (lv_font_conv messages, glyph notes)
```

The output is deterministic: the same sources and pinned tool versions give
byte-identical files. Asset changes go in **one commit**: source file +
`assets.toml` + regenerated `src/ui/generated/`.

The Material Symbols variable font (15 MB) is not committed. The first run
downloads it from the pinned `raw.githubusercontent.com` URL in `assets.toml`,
checks the SHA-256 and caches it in `tools/.cache/` (gitignored). Behind a
TLS-intercepting proxy, point Python at its CA bundle with `SSL_CERT_FILE=…`
(`REQUESTS_CA_BUNDLE` is honoured as a fallback). Never turn verification off.

### What gets generated

| Manifest | Symbols in `assets.h` | File |
| --- | --- | --- |
| `[[font]] name = "body"` | `LV_FONT_DECLARE(font_body)` | `fonts/font_body.c` |
| `icons = [...]` in an icon font | `#define ICON_LIGHTBULB "\xEE\xA4\x8F"` (UTF-8) | — |
| `[[image]] name = "x"` (flash) | `LV_IMAGE_DECLARE(img_x)` | `images/img_x.c` |
| `[[image]] name = "x"`, `dest = "fs"` | — (load `A:/img/x.bin`) | `data/img/x.bin` |
| `[[sprite]] name = "s"` | `sprite_s_frames[]`, `SpriteInfo sprite_s`, `sprite_s_fNN` | `sprites/sprite_s*.c` |

Use it as `#include "ui/generated/assets.h"` (path relative to `src/`), then
e.g. `lv_obj_set_style_text_font(label, &font_icon_28, 0);
lv_label_set_text(label, ICON_WIFI);`.

`SpriteInfo` is `{frames, count, fps, w, h, loop}` with `loop` one of
`SPRITE_LOOP` (0), `SPRITE_ONCE` (1), `SPRITE_PINGPONG` (2).

### Fonts: how they're converted

- `lv_font_conv` with `--bpp 4 --no-compress --no-prefilter --lv-include lvgl.h`
  and `--force-fast-kern-format` for text (the same flags LVGL uses for its
  built-in fonts) or `--no-kerning` for icon fonts. Uncompressed bitmaps
  render faster and need no `LV_USE_FONT_COMPRESSED`; add `compress = true` to
  a font entry to trade CPU for flash.
- `lv_font_conv` 1.5.3 predates LVGL 9.3/9.6, so `assets.py` adds the fields
  newer converters write: `.static_bitmap = 1` and `.cap_height` / `.x_height`
  (from the font's OS/2 table, for `LV_TEXT_LEADING_TRIM_*`).
- Variable fonts (`source = "…"` + `variations = {…}`) are subset to the
  requested codepoints, every axis is pinned with
  `fontTools.varLib.instancer`, and the font's default substitutions are
  baked into the cmap. That matters for Material Symbols: `FILL = 1` swaps in
  separate `*.fill` glyphs through GSUB feature variations, which
  `lv_font_conv` (no shaping) would otherwise ignore.
- Text fonts warn (with `-v`) about requested codepoints the font lacks; Inter
  has no U+00AD (soft hyphen) or U+0149 (deprecated ŉ), which is fine.

### Add a font

1. Put the `.ttf`/`.otf` in `assets/fonts/` with its licence file, note where
   it came from in the `assets.toml` header, and add it to `docs/LIBRARIES.md`.
2. Add a `[[font]]`:

   ```toml
   [[font]]
   name    = "display"                 # -> font_display
   file    = "fonts/Inter-SemiBold.ttf"
   size    = 48
   bpp     = 4
   range   = ["0x20-0x7E", "0xA0-0x17F"]   # ranges/single codepoints
   symbols = "0123456789"              # and/or literal characters
   ```

3. `python tools/assets.py`, commit font + manifest + output.

### Add an icon

Find the name in the official list (the `.codepoints` file linked from
`[source.material_symbols_rounded]` in `assets.toml`, or
<https://fonts.google.com/icons?icon.style=Rounded>), add it to `icons` of
**every** `icon_*` font, regenerate. Use it as `ICON_<NAME>`. A misspelt
name fails the build with the list it was looked up in.

To move to a newer Material Symbols release, update the commit in both URLs
and both SHA-256 values (`sha256sum` the downloaded files), regenerate, and
check the icons still look right.

### Add an image

```toml
[[image]]
name   = "bulb_on"            # -> img_bulb_on
file   = "images/bulb_on.png" # sRGB PNG at 1:1 pixel size
format = "RGB565A8"           # RGB565 | RGB565A8 | A8 | A4 | … (any LVGLImage --cf)
dest   = "flash"              # flash (C array) | fs (data/img/bulb_on.bin)
# compress = "LZ4"            # fs only: NONE | RLE | LZ4
# dither = true               # RGB565 dithering for gradients
```

### Add a sprite

A sprite is a PNG grid of equal frames, read row by row, left to right:

```toml
[[sprite]]
name   = "steam"              # -> sprite_steam, sprite_steam_frames[]
file   = "sprites/steam/sheet.png"
frame  = [64, 64]             # w, h of one frame
frames = 12                   # may be fewer than the grid holds
fps    = 12
loop   = "loop"               # loop | once | pingpong
format = "RGB565A8"
```

Each frame becomes its own `lv_image_dsc_t` (`sprite_steam_f00`, …). Keep
sprites small (64×64 × 12 frames RGB565A8 ≈ 147 KB of flash).

### Upgrading the converters

- **LVGL**: copy `scripts/LVGLImage.py` from the new tag into
  `tools/lvgl/LVGLImage.py` (keep the header, update the tag/commit in it).
- **lv_font_conv**: change the version in `package.json` and
  `LV_FONT_CONV_VERSION` in `assets.py`, run `npm install --prefix tools`,
  commit `package-lock.json`, regenerate and review the diff.
- **fontTools**: bump `requirements.txt`, regenerate, review the icon diff.
