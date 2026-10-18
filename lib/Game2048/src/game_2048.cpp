#include "game_2048.h"

namespace g2048 {

uint32_t Rng::next() {
  if (!s) s = 0x9E3779B9u;  // xorshift32 is stuck at 0
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

uint32_t Rng::below(uint32_t n) {
  const uint32_t limit = UINT32_MAX - UINT32_MAX % n;  // reject the uneven tail
  uint32_t r;
  do r = next();
  while (r >= limit);
  return r % n;
}

void clear(Board &b) { b = Board{}; }

void newGame(Board &b, Rng &rng) {
  clear(b);
  spawn(b, rng);
  spawn(b, rng);
}

namespace {

// The cells of line `i` for a move toward `d`, starting at the wall the tiles move to.
void line(Dir d, int i, uint8_t out[kSide]) {
  for (int k = 0; k < kSide; k++) {
    switch (d) {
      case Dir::Left: out[k] = uint8_t(i * kSide + k); break;
      case Dir::Right: out[k] = uint8_t(i * kSide + kSide - 1 - k); break;
      case Dir::Up: out[k] = uint8_t(k * kSide + i); break;
      case Dir::Down: out[k] = uint8_t((kSide - 1 - k) * kSide + i); break;
    }
  }
}

}  // namespace

Move slide(Board &b, Dir d) {
  Move m{};
  for (int i = 0; i < kSide; i++) {
    uint8_t idx[kSide];
    line(d, i, idx);
    uint8_t out[kSide] = {};
    bool fresh[kSide] = {};  // out[k] is a plain tile that may still merge
    int write = 0;
    for (int read = 0; read < kSide; read++) {
      const uint8_t e = b.cell[idx[read]];
      if (!e) continue;
      if (write > 0 && fresh[write - 1] && out[write - 1] == e) {
        out[write - 1] = uint8_t(e + 1);
        fresh[write - 1] = false;  // a tile merges once per move
        m.slides[m.nSlides++] = {idx[read], idx[write - 1], true};
        m.mergedAt[m.nMerged++] = idx[write - 1];
        m.gain += value(uint8_t(e + 1));
        if (e + 1 >= kWinExp && !b.won) {
          b.won = 1;
          m.justWon = true;
        }
        m.moved = true;
      } else {
        out[write] = e;
        fresh[write] = true;
        m.slides[m.nSlides++] = {idx[read], idx[write], false};
        if (read != write) m.moved = true;
        write++;
      }
    }
    for (int k = 0; k < kSide; k++) b.cell[idx[k]] = out[k];
  }
  b.score += m.gain;
  return m;
}

int spawn(Board &b, Rng &rng) {
  uint8_t empty[kCells];
  int n = 0;
  for (int i = 0; i < kCells; i++)
    if (!b.cell[i]) empty[n++] = uint8_t(i);
  if (!n) return -1;
  const int at = empty[rng.below(uint32_t(n))];
  b.cell[at] = rng.below(10) == 0 ? 2 : 1;
  return at;
}

bool over(const Board &b) {
  for (int i = 0; i < kCells; i++) {
    if (!b.cell[i]) return false;
    const int r = i / kSide, c = i % kSide;
    if (c + 1 < kSide && b.cell[i] == b.cell[i + 1]) return false;
    if (r + 1 < kSide && b.cell[i] == b.cell[i + kSide]) return false;
  }
  return true;
}

int tiles(const Board &b) {
  int n = 0;
  for (uint8_t e : b.cell) n += e != 0;
  return n;
}

uint8_t maxExp(const Board &b) {
  uint8_t m = 0;
  for (uint8_t e : b.cell)
    if (e > m) m = e;
  return m;
}

}  // namespace g2048
