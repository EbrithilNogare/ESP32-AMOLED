#include "wire_raster.h"

#include <math.h>

namespace raster {

uint16_t rgb565(uint32_t rgb) {
  return uint16_t(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

static inline uint16_t swap(uint16_t v) { return uint16_t((v >> 8) | (v << 8)); }

void fill(Canvas &c, int x0, int y0, int x1, int y1, uint16_t color) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > c.w) x1 = c.w;
  if (y1 > c.h) y1 = c.h;
  const uint16_t v = c.swapped ? swap(color) : color;
  for (int y = y0; y < y1; y++) {
    uint16_t *row = c.px + y * c.w;
    for (int x = x0; x < x1; x++) row[x] = v;
  }
}

// RGB565 blend with alpha 0–32: the channels spread over 32 bits so one
// multiply handles all three.
static inline void blend(Canvas &c, int x, int y, uint32_t fg32, uint32_t a32) {
  uint16_t &p = c.px[y * c.w + x];
  const uint16_t bg = c.swapped ? swap(p) : p;
  const uint32_t bg32 = (bg | (uint32_t(bg) << 16)) & 0x07E0F81F;
  const uint32_t out = (bg32 + (((fg32 - bg32) * a32) >> 5)) & 0x07E0F81F;
  const uint16_t v = uint16_t(out | (out >> 16));
  p = c.swapped ? swap(v) : v;
}

// How much of [a, a + 1) lies within [lo, hi].
static inline float overlap(float a, float lo, float hi) {
  const float l = a > lo ? a : lo, h = a + 1 < hi ? a + 1 : hi;
  return h > l ? h - l : 0;
}

void line(Canvas &c, float x0, float y0, float x1, float y1, float width, uint32_t rgb, uint8_t opa) {
  if (!opa || width <= 0) return;
  float dx = x1 - x0, dy = y1 - y0;
  if (fabsf(dx) < 1e-3f && fabsf(dy) < 1e-3f) return;
  const uint16_t color = rgb565(rgb);
  const uint32_t fg32 = (color | (uint32_t(color) << 16)) & 0x07E0F81F;

  // Walk the major axis (u), spanning the minor axis (v) per step. Swapping
  // the roles turns a steep line into a flat one.
  const bool steep = fabsf(dy) > fabsf(dx);
  float u0 = steep ? y0 : x0, u1 = steep ? y1 : x1, v0 = steep ? x0 : y0, v1 = steep ? x1 : y1;
  if (u1 < u0) {
    float t = u0;
    u0 = u1, u1 = t;
    t = v0;
    v0 = v1, v1 = t;
  }
  const float slope = (v1 - v0) / (u1 - u0);
  const float half = width * 0.5f * sqrtf(1 + slope * slope);  // thickness across the minor axis
  const int uLimit = steep ? c.h : c.w, vLimit = steep ? c.w : c.h;
  int ua = int(floorf(u0)), ub = int(floorf(u1));
  if (ua < 0) ua = 0;
  if (ub > uLimit - 1) ub = uLimit - 1;
  for (int u = ua; u <= ub; u++) {
    const float along = overlap(float(u), u0, u1);  // partial first and last step
    if (along <= 0) continue;
    const float uc = u + 0.5f < u0 ? u0 : u + 0.5f > u1 ? u1 : u + 0.5f;
    const float vc = v0 + (uc - u0) * slope, lo = vc - half, hi = vc + half;
    int va = int(floorf(lo)), vb = int(floorf(hi));
    if (va < 0) va = 0;
    if (vb > vLimit - 1) vb = vLimit - 1;
    for (int v = va; v <= vb; v++) {
      const float cov = overlap(float(v), lo, hi) * along;
      const uint32_t a = uint32_t(cov * opa * (32.0f / 255.0f) + 0.5f);
      if (!a) continue;
      if (steep)
        blend(c, v, u, fg32, a);
      else
        blend(c, u, v, fg32, a);
    }
  }
}

}  // namespace raster
