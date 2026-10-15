#include "touch_cal.h"

namespace touchcal {

static int16_t apply(int32_t scale, int32_t offset, int16_t raw) {
  const int64_t v = (int64_t(raw) * scale + (scale >= 0 ? 32768 : -32768)) / 65536 + offset;
  return int16_t(v < INT16_MIN ? INT16_MIN : v > INT16_MAX ? INT16_MAX : v);
}

int16_t mapX(const Cal &c, int16_t raw) { return apply(c.xScale, c.xOffset, raw); }
int16_t mapY(const Cal &c, int16_t raw) { return apply(c.yScale, c.yOffset, raw); }

bool plausible(const Cal &c) {
  auto ok = [](int32_t scale, int32_t offset) {
    return scale >= 65536 * 8 / 10 && scale <= 65536 * 12 / 10 && offset >= -kMaxOffset && offset <= kMaxOffset;
  };
  return ok(c.xScale, c.xOffset) && ok(c.yScale, c.yOffset);
}

// target = scale * raw + offset over the samples, one axis (x: true).
static bool fitAxis(const Sample *s, int n, bool x, int32_t &scale, int32_t &offset) {
  int64_t sr = 0, st = 0, srr = 0, srt = 0;
  for (int i = 0; i < n; i++) {
    const int64_t r = x ? s[i].rawX : s[i].rawY, t = x ? s[i].x : s[i].y;
    sr += r;
    st += t;
    srr += r * r;
    srt += r * t;
  }
  const int64_t den = n * srr - sr * sr;
  if (den <= 0) return false;  // all samples at one raw position
  const int64_t num = n * srt - sr * st;
  scale = int32_t((num * 65536 + den / 2) / den);
  const int64_t off = (st * 65536 - int64_t(scale) * sr) / n;  // offset in 1/65536 px
  offset = int32_t((off + (off >= 0 ? 32768 : -32768)) / 65536);
  return true;
}

bool fit(const Sample *s, int n, int32_t maxMissPx, Cal &out) {
  if (n < 2) return false;
  Cal c;
  if (!fitAxis(s, n, true, c.xScale, c.xOffset) || !fitAxis(s, n, false, c.yScale, c.yOffset)) return false;
  if (!plausible(c)) return false;
  for (int i = 0; i < n; i++) {
    const int32_t dx = mapX(c, s[i].rawX) - s[i].x, dy = mapY(c, s[i].rawY) - s[i].y;
    if (dx * dx + dy * dy > maxMissPx * maxMissPx) return false;
  }
  out = c;
  return true;
}

}  // namespace touchcal
