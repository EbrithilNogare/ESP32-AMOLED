#!/usr/bin/env python3
"""Draw the Magic 8-ball's red paint swipe (docs/apps/magic8.md) as an alpha
mask: a dry-brush stroke with ragged edges and bristle streaks, tilted -4°.
Deterministic (fixed seed), so the committed PNG can be regenerated:

    python tools/gen_brush.py        → assets/images/brush.png

The firmware draws it as an A8 image tinted with MAGIC8_COLOR. Needs Pillow.
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
W, H = 340, 140          # output size
SW, SH = 320, 96         # the stroke before the tilt
TILT = 4                 # degrees, counter-clockwise up on the right
SCALE = 3                # drawn at 3x, then downsampled: smooth rough edges


def noise1d(rng: random.Random, n: int, octaves: list[tuple[int, float]]) -> list[float]:
    """Sum of smoothly interpolated random curves: (control points, amplitude)."""
    out = [0.0] * n
    for points, amp in octaves:
        ctrl = [rng.uniform(-1, 1) for _ in range(points + 1)]
        for i in range(n):
            t = i / (n - 1) * points
            k = min(int(t), points - 1)
            f = t - k
            f = f * f * (3 - 2 * f)
            out[i] += amp * (ctrl[k] * (1 - f) + ctrl[k + 1] * f)
    return out


def main() -> None:
    rng = random.Random(803)
    w, h = SW * SCALE, SH * SCALE
    img = Image.new("L", (w, h), 0)
    px = img.load()
    top = noise1d(rng, w, [(6, 7 * SCALE), (40, 3 * SCALE), (160, 1.2 * SCALE)])
    bot = noise1d(rng, w, [(6, 7 * SCALE), (40, 3 * SCALE), (160, 1.2 * SCALE)])
    # Bristle streaks: each row of the brush lays down a slightly different
    # amount of paint, starts a little later or earlier, and runs dry at its
    # own point toward the right, breaking up into dashes before it ends.
    row_amt = noise1d(rng, h, [(8, 0.06), (h // 6, 0.05)])
    row_start = noise1d(rng, h, [(5, 0.5), (h // 10, 0.5)])
    row_end = noise1d(rng, h, [(4, 0.5), (h // 4, 0.5)])
    seg = noise1d(rng, w, [(30, 0.6), (90, 0.4)])
    for y in range(h):
        amt = 0.9 + row_amt[y]
        if rng.random() < 0.08:
            amt *= rng.uniform(0.55, 0.8)  # a thin, dry bristle
        x0 = w * (0.015 + 0.02 * (row_start[y] * 0.5 + 0.5))
        x1 = w * (0.86 + 0.13 * (row_end[y] * 0.5 + 0.5))
        dash = rng.uniform(-0.4, 0.4)  # where this row's dashes break
        for x in range(int(x0), min(w, int(x1))):
            u = x / (w - 1)
            half = h * 0.5 * (0.96 - 0.12 * u)
            if not (h / 2 - half + top[x] <= y <= h / 2 + half + bot[x]):
                continue
            a = amt
            dry = (x - (x1 - w * 0.12)) / (w * 0.12)  # 0 → 1 over the last 12 %
            if dry > 0:
                if seg[x] + dash > 0.55 - 0.5 * dry:
                    continue
                a *= 1 - 0.5 * dry
            px[x, y] = int(255 * min(1.0, a))
    # A few loose bristle trails past the end of the stroke.
    draw = ImageDraw.Draw(img)
    for _ in range(9):
        y = rng.uniform(h * 0.2, h * 0.8)
        x0 = w * rng.uniform(0.9, 0.95)
        length = w * rng.uniform(0.04, 0.1)
        draw.line([(x0, y), (min(w - 1, x0 + length), y + rng.uniform(-6, 6))],
                  fill=int(255 * rng.uniform(0.4, 0.8)), width=rng.randint(2, 5))
    img = img.filter(ImageFilter.GaussianBlur(0.8 * SCALE / 2))
    img = img.resize((SW, SH), Image.LANCZOS)
    out = Image.new("L", (W, H), 0)
    out.paste(img, ((W - SW) // 2, (H - SH) // 2))
    out = out.rotate(TILT, resample=Image.BICUBIC, center=(W / 2, H / 2))
    # Store as grey + alpha with white colour: the alpha is the mask.
    rgba = Image.merge("RGBA", (Image.new("L", (W, H), 255),) * 3 + (out,))
    path = ROOT / "assets" / "images" / "brush.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    rgba.save(path, optimize=True)
    print(f"wrote {path} ({W}x{H})")


if __name__ == "__main__":
    main()
