"""Turn the simulator's PPM frames into PNGs plus one contact sheet.

    pio run -e sim -t exec            # writes .pio/sim/*.ppm
    python tools/sim/shots.py         # → docs/design/screens/*.png + docs/design/firmware-screens.png

Needs Pillow (pip install pillow).
"""
import argparse
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=ROOT / ".pio" / "sim", type=pathlib.Path)
    ap.add_argument("--out", default=ROOT / "docs" / "design", type=pathlib.Path)
    ap.add_argument("--cols", default=6, type=int)
    args = ap.parse_args()

    frames = sorted(args.src.glob("*.ppm"))
    if not frames:
        raise SystemExit(f"no frames in {args.src}; run the simulator first")
    screens = args.out / "screens"
    screens.mkdir(parents=True, exist_ok=True)

    images = []
    for f in frames:
        img = Image.open(f).convert("RGB")
        img.save(screens / (f.stem + ".png"), optimize=True)
        images.append((f.stem, img))

    # Contact sheet: half-size frames with a caption under each.
    w, h = images[0][1].size
    tw, th, cap, gap = w // 2, h // 2, 22, 12
    rows = (len(images) + args.cols - 1) // args.cols
    sheet = Image.new("RGB", (args.cols * (tw + gap) + gap, rows * (th + cap + gap) + gap), (14, 17, 20))
    draw = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(images):
        x = gap + (i % args.cols) * (tw + gap)
        y = gap + (i // args.cols) * (th + cap + gap)
        sheet.paste(img.resize((tw, th), Image.LANCZOS), (x, y))
        draw.text((x, y + th + 4), name.split("-", 1)[-1], fill=(150, 160, 171))
    sheet.save(args.out / "firmware-screens.png", optimize=True)
    print(f"{len(images)} screens → {screens}, sheet → {args.out / 'firmware-screens.png'}")


if __name__ == "__main__":
    main()
