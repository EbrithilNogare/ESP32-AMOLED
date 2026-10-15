// Anti-aliased lines straight into an RGB565 buffer, for wireframes with
// hundreds of edges (the Dice app). LVGL's line drawing masks every pixel of
// a line's bounding box; this walks the line once and touches only the
// pixels it covers, so a frame of 2 × 10 d20 costs a fraction.
#pragma once

#include <stdint.h>

namespace raster {

struct Canvas {
  uint16_t *px;  // w × h, row-major, no padding
  int w, h;
  bool swapped;  // pixels stored byte-swapped (LV_COLOR_FORMAT_RGB565_SWAPPED)
};

uint16_t rgb565(uint32_t rgb);  // 0xRRGGBB → RGB565 (not swapped)

// The rectangle [x0, x1) × [y0, y1), clipped to the canvas.
void fill(Canvas &c, int x0, int y0, int x1, int y1, uint16_t color);

// A line of `width` px from (x0, y0) to (x1, y1) with square ends at the
// end points, blended over what's there with `opa` (0–255). Pixel centres are
// at +0.5: a horizontal line at y = 10 with width 2 covers rows 9 and 10.
void line(Canvas &c, float x0, float y0, float x1, float y1, float width, uint32_t rgb, uint8_t opa);

}  // namespace raster
