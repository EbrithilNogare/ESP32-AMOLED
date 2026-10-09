#include "dice_core.h"

#include <math.h>

namespace dice {

static constexpr float kPi = 3.14159265358979f;
static constexpr float kPhi = 1.61803398874989f;
static constexpr float kD2R = kPi / 180.0f;

bool validDie(uint8_t die) {
  for (uint8_t d : kDice)
    if (d == die) return true;
  return false;
}

int maxCount(uint8_t die) { return die == 100 ? kMaxCountD100 : kMaxCount; }

uint8_t uniform(Rng rng, uint8_t n) {
  // Reject the top partial bucket so every face is exactly equally likely.
  const uint32_t limit = UINT32_MAX - UINT32_MAX % n;
  uint32_t r;
  do r = rng();
  while (r >= limit);
  return uint8_t(r % n + 1);
}

static void finish(Roll &r) {
  for (int s = 0; s < r.sets; s++) {
    r.total[s] = 0;
    for (int i = 0; i < r.count; i++) r.total[s] += r.v[s][i];
  }
  r.kept = 0;
  if (r.sets == 2) {
    if (r.mode == Mode::Adv && r.total[1] > r.total[0]) r.kept = 1;
    if (r.mode == Mode::Dis && r.total[1] < r.total[0]) r.kept = 1;
  }
}

static Roll base(uint8_t die, uint8_t count, Mode mode) {
  Roll r;
  r.die = validDie(die) ? die : 20;
  r.count = count < 1 ? 1 : count > maxCount(r.die) ? uint8_t(maxCount(r.die)) : count;
  r.mode = mode;
  r.sets = mode == Mode::Flat ? 1 : 2;
  return r;
}

Roll roll(uint8_t die, uint8_t count, Mode mode, Rng rng) {
  Roll r = base(die, count, mode);
  for (int s = 0; s < r.sets; s++)
    for (int i = 0; i < r.count; i++) r.v[s][i] = uniform(rng, r.die);
  finish(r);
  if (r.die == 20 && r.count == 1) {
    const uint8_t v = r.v[r.kept][0];
    r.crit = v == 20 ? Crit::Nat20 : v == 1 ? Crit::Nat1 : Crit::None;
  }
  return r;
}

Roll idle(uint8_t die, uint8_t count, Mode mode) {
  Roll r = base(die, count, mode);
  for (int s = 0; s < r.sets; s++)
    for (int i = 0; i < r.count; i++) r.v[s][i] = r.die;
  finish(r);
  return r;
}

Percentile split(uint8_t value) { return {uint8_t((value % 100) / 10 * 10), uint8_t(value % 10)}; }

// ---------------------------------------------------------------- 3D math

static Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 scale(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len(Vec3 a) { return sqrtf(dot(a, a)); }
static Vec3 unit(Vec3 a) { return scale(a, 1.0f / len(a)); }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Vec3 mul(const Mat3 &a, const Vec3 &v) {
  return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
          a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

Mat3 mul(const Mat3 &a, const Mat3 &b) {
  Mat3 r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
  return r;
}

Mat3 axisAngle(const Vec3 &a, float th) {
  const float c = cosf(th), s = sinf(th), t = 1 - c;
  return {{{t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y},
           {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x},
           {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c}}};
}

Vec3 randomAxis(float u1, float u2) {
  const float z = u1 * 2 - 1, a = u2 * 2 * kPi, s = sqrtf(1 - z * z);
  return {s * cosf(a), s * sinf(a), z};
}

float easeOutQuart(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const float u = 1 - t;
  return 1 - u * u * u * u;
}

// ---------------------------------------------------------------- solids

static void addEdge(Solid &s, int i, int j) {
  if (s.ne < kMaxEdges) {
    s.e[s.ne][0] = uint8_t(i);
    s.e[s.ne][1] = uint8_t(j);
    s.ne++;
  }
}

// Rest pose: the rotation that maps the face normal n to (0, 0, -1) and the
// face's "up" direction u to screen up (0, -1, 0). Built as T·Sᵀ with
// S = [n u n×u] and T = [(0,0,-1) (0,-1,0) (-1,0,0)], which keeps it a proper
// rotation (T's third column is the cross product of the first two).
static void setRest(Solid &s, Vec3 n, Vec3 u) {
  u = unit(sub(u, scale(n, dot(u, n))));
  const Vec3 w = cross(n, u);
  const float tn[3] = {0, 0, -1}, tu[3] = {0, -1, 0}, tw[3] = {-1, 0, 0};
  const float nn[3] = {n.x, n.y, n.z}, uu[3] = {u.x, u.y, u.z}, ww[3] = {w.x, w.y, w.z};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) s.rest.m[i][j] = tn[i] * nn[j] + tu[i] * uu[j] + tw[i] * ww[j];

  // Order the face vertices by angle around the screen centre for filling.
  float ang[kMaxFaceVerts];
  for (int k = 0; k < s.nface; k++) {
    const Vec3 p = mul(s.rest, s.v[s.face[k]]);
    ang[k] = atan2f(p.y, p.x);
  }
  for (int a = 1; a < s.nface; a++)
    for (int b = a; b > 0 && ang[b] < ang[b - 1]; b--) {
      const float ta = ang[b];
      ang[b] = ang[b - 1];
      ang[b - 1] = ta;
      const uint8_t tf = s.face[b];
      s.face[b] = s.face[b - 1];
      s.face[b - 1] = tf;
    }
}

static void build(Solid &s, uint8_t die) {
  s.die = die;
  s.nv = s.ne = s.nface = 0;
  auto vert = [&](float x, float y, float z) { s.v[s.nv++] = {x, y, z}; };
  if (die == 4) {
    vert(1, 1, 1), vert(1, -1, -1), vert(-1, 1, -1), vert(-1, -1, 1);
  } else if (die == 6) {
    for (int a = -1; a <= 1; a += 2)
      for (int b = -1; b <= 1; b += 2)
        for (int c = -1; c <= 1; c += 2) vert(a, b, c);
  } else if (die == 8) {
    vert(1, 0, 0), vert(-1, 0, 0), vert(0, 1, 0), vert(0, -1, 0), vert(0, 0, 1), vert(0, 0, -1);
  } else if (die == 12) {
    for (int a = -1; a <= 1; a += 2)
      for (int b = -1; b <= 1; b += 2)
        for (int c = -1; c <= 1; c += 2) vert(a, b, c);
    for (int a = -1; a <= 1; a += 2)
      for (int b = -1; b <= 1; b += 2) {
        vert(0, a / kPhi, b * kPhi);
        vert(a / kPhi, b * kPhi, 0);
        vert(a * kPhi, 0, b / kPhi);
      }
  } else if (die == 20) {
    for (int a = -1; a <= 1; a += 2)
      for (int b = -1; b <= 1; b += 2) {
        vert(0, a, b * kPhi);
        vert(a, b * kPhi, 0);
        vert(a * kPhi, 0, b);
      }
  } else {  // d10: pentagonal trapezohedron, apexes on the y axis (index 0 = top)
    vert(0, -1.25f, 0), vert(0, 1.25f, 0);
    for (int i = 0; i < 5; i++) vert(cosf(72 * i * kD2R), -0.13f, sinf(72 * i * kD2R));
    for (int i = 0; i < 5; i++) vert(cosf((72 * i + 36) * kD2R), 0.13f, sinf((72 * i + 36) * kD2R));
    for (int i = 0; i < 5; i++) {
      addEdge(s, 0, 2 + i);
      addEdge(s, 1, 7 + i);
      addEdge(s, 2 + i, 7 + i);
      addEdge(s, 2 + i, 7 + (i + 4) % 5);
    }
  }
  float m = 0;
  for (int i = 0; i < s.nv; i++) m = fmaxf(m, len(s.v[i]));
  for (int i = 0; i < s.nv; i++) s.v[i] = scale(s.v[i], 1.0f / m);

  if (s.ne == 0) {  // regular solids: edges are the vertex pairs at minimum distance
    float min = 1e9f;
    for (int i = 0; i < s.nv; i++)
      for (int j = i + 1; j < s.nv; j++) min = fminf(min, len(sub(s.v[i], s.v[j])));
    for (int i = 0; i < s.nv; i++)
      for (int j = i + 1; j < s.nv; j++)
        if (len(sub(s.v[i], s.v[j])) < min * 1.01f) addEdge(s, i, j);
  }

  if (die == 10) {
    // Kite face: top apex, ring vertex, the lower vertex between, next ring vertex.
    const uint8_t f[4] = {0, 2, 7, 3};
    for (uint8_t k : f) s.face[s.nface++] = k;
    const Vec3 c = scale(add(add(s.v[0], s.v[2]), add(s.v[7], s.v[3])), 0.25f);
    Vec3 n = unit(cross(sub(s.v[3], s.v[2]), sub(s.v[7], s.v[0])));
    if (dot(n, c) < 0) n = scale(n, -1);
    setRest(s, n, sub(s.v[0], c));  // apex up
    return;
  }
  Vec3 dir;
  switch (die) {
    case 4: dir = scale(s.v[0], -1); break;  // the face opposite vertex 0
    case 6: dir = {0, 0, 1}; break;
    case 12: dir = {0, kPhi, 1}; break;  // a dodecahedron face centre (dual icosahedron vertex)
    default: dir = {1, 1, 1}; break;     // octahedron and icosahedron
  }
  const Vec3 n = unit(dir);
  float best = -1e9f;
  for (int i = 0; i < s.nv; i++) best = fmaxf(best, dot(s.v[i], n));
  Vec3 c = {0, 0, 0};
  for (int i = 0; i < s.nv && s.nface < kMaxFaceVerts; i++)
    if (dot(s.v[i], n) > best - 1e-4f) {
      s.face[s.nface++] = uint8_t(i);
      c = add(c, s.v[i]);
    }
  c = scale(c, 1.0f / s.nface);
  Vec3 up;
  if (die == 6) {
    up = {0, -1, 0};  // cube: an edge on top, not a corner
  } else {            // otherwise the face vertex highest on screen points up
    int top = s.face[0];
    for (int k = 1; k < s.nface; k++)
      if (s.v[s.face[k]].y < s.v[top].y) top = s.face[k];
    up = sub(s.v[top], c);
  }
  setRest(s, n, up);
}

const Solid &solid(uint8_t die) {
  static Solid cache[6];
  static bool built[6];
  static const uint8_t kSolids[6] = {4, 6, 8, 10, 12, 20};
  int k = 5;
  for (int i = 0; i < 6; i++)
    if (kSolids[i] == die) k = i;
  if (die == 100) k = 3;
  if (!built[k]) {
    build(cache[k], kSolids[k]);
    built[k] = true;
  }
  return cache[k];
}

Mat3 spinPose(const Solid &s, const Vec3 &axis, float turns, float progress) {
  if (progress >= 1) return s.rest;
  return mul(s.rest, axisAngle(axis, turns * (1 - easeOutQuart(progress))));
}

void project(const Solid &s, const Mat3 &pose, float cx, float cy, float r, Point *out) {
  for (int i = 0; i < s.nv; i++) {
    const Vec3 q = mul(pose, s.v[i]);
    const float k = 4.0f / (4.0f + q.z);
    out[i] = {cx + q.x * r * k, cy + q.y * r * k, q.z};
  }
}

// ---------------------------------------------------------------- icons

static int seg(Segment *out, int n, float x1, float y1, float x2, float y2, bool inner) {
  if (n < kMaxIconSegments) out[n] = {x1, y1, x2, y2, inner};
  return n + 1;
}

static int polygon(Segment *out, int n, const float (*p)[2], int count, float k, float dx) {
  for (int i = 0; i < count; i++) {
    const int j = (i + 1) % count;
    n = seg(out, n, p[i][0] * k + dx, p[i][1] * k, p[j][0] * k + dx, p[j][1] * k, false);
  }
  return n;
}

static int iconOne(uint8_t die, Segment *out, int n, float k, float dx) {
  float p[10][2];
  auto pol = [](float deg, float r, float *xy) {
    xy[0] = cosf(deg * kD2R) * r;
    xy[1] = sinf(deg * kD2R) * r;
  };
  auto inner = [&](float x1, float y1, float x2, float y2) {
    return seg(out, n, x1 * k + dx, y1 * k, x2 * k + dx, y2 * k, true);
  };
  switch (die) {
    case 4:
      for (int i = 0; i < 3; i++) pol(-90 + 120 * i, 1.08f, p[i]), p[i][1] += 0.2f;
      n = polygon(out, n, p, 3, k, dx);
      for (int i = 0; i < 3; i++) n = inner(0, 0.2f, p[i][0], p[i][1]);
      break;
    case 6:
      for (int i = 0; i < 4; i++) pol(45 + 90 * i, 1, p[i]);
      n = polygon(out, n, p, 4, k, dx);
      break;
    case 8:
      for (int i = 0; i < 6; i++) pol(-90 + 60 * i, 1, p[i]);
      n = polygon(out, n, p, 6, k, dx);
      for (int i = 0; i < 6; i += 2) n = inner(p[i][0], p[i][1], p[(i + 2) % 6][0], p[(i + 2) % 6][1]);
      break;
    case 10: {
      const float o[4][2] = {{0, -1}, {0.92f, 0.1f}, {0, 1}, {-0.92f, 0.1f}};
      n = polygon(out, n, o, 4, k, dx);
      const float t[2] = {0, -1}, fr[2] = {0.56f, 0}, m[2] = {0, 0.42f}, fl[2] = {-0.56f, 0};
      n = inner(t[0], t[1], fr[0], fr[1]);
      n = inner(fr[0], fr[1], m[0], m[1]);
      n = inner(m[0], m[1], fl[0], fl[1]);
      n = inner(fl[0], fl[1], t[0], t[1]);
      n = inner(fr[0], fr[1], o[1][0], o[1][1]);
      n = inner(fl[0], fl[1], o[3][0], o[3][1]);
      n = inner(m[0], m[1], o[2][0], o[2][1]);
      break;
    }
    case 12: {
      for (int i = 0; i < 10; i++) pol(-90 + 36 * i, 1, p[i]);
      n = polygon(out, n, p, 10, k, dx);
      float q[5][2];
      for (int i = 0; i < 5; i++) pol(-90 + 72 * i, 0.6f, q[i]);
      for (int i = 0; i < 5; i++) {
        n = inner(q[i][0], q[i][1], q[(i + 1) % 5][0], q[(i + 1) % 5][1]);
        n = inner(q[i][0], q[i][1], p[2 * i][0], p[2 * i][1]);
      }
      break;
    }
    default: {  // d20
      for (int i = 0; i < 6; i++) pol(-90 + 60 * i, 1, p[i]);
      n = polygon(out, n, p, 6, k, dx);
      float t[3][2];
      for (int i = 0; i < 3; i++) pol(-90 + 120 * i, 0.58f, t[i]);
      for (int i = 0; i < 3; i++) {
        n = inner(t[i][0], t[i][1], t[(i + 1) % 3][0], t[(i + 1) % 3][1]);
        for (int d = -1; d <= 1; d++) {
          const int h = (2 * i + d + 6) % 6;
          n = inner(t[i][0], t[i][1], p[h][0], p[h][1]);
        }
      }
      break;
    }
  }
  return n;
}

int icon(uint8_t die, Segment *out) {
  int n = die == 100 ? iconOne(10, out, iconOne(10, out, 0, 0.62f, -0.55f), 0.62f, 0.55f) : iconOne(die, out, 0, 1, 0);
  return n < kMaxIconSegments ? n : kMaxIconSegments;
}

// ---------------------------------------------------------------- layout

float layoutGrid(int n, float x, float y, float w, float h, float maxR, float aspect, Slot *out) {
  if (n < 1) return 0;
  int bestCols = 1;
  float bestCh = 0;
  for (int cols = 1; cols <= n; cols++) {
    const int rows = (n + cols - 1) / cols;
    const float ch = fminf(w / (cols * aspect), h / rows);
    if (ch > bestCh) {
      bestCh = ch;
      bestCols = cols;
    }
  }
  const int rows = (n + bestCols - 1) / bestCols;
  const float cw = bestCh * aspect, r = fminf(maxR, bestCh * 0.42f);
  const float y0 = y + (h - rows * bestCh) / 2;
  for (int i = 0; i < n; i++) {
    const int row = i / bestCols, col = i % bestCols;
    const int inRow = n - row * bestCols < bestCols ? n - row * bestCols : bestCols;
    const float x0 = x + (w - inRow * cw) / 2;
    out[i] = {x0 + cw * (col + 0.5f), y0 + bestCh * (row + 0.5f), r};
  }
  return r;
}

}  // namespace dice
