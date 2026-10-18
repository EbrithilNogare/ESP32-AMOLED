// 2048 (docs/apps/g2048.md): the board, slides and merges, with every tile's
// movement reported so the app can animate it. No LVGL, no Arduino.
#pragma once

#include <stdint.h>

namespace g2048 {

constexpr int kSide = 4;
constexpr int kCells = kSide * kSide;
constexpr uint8_t kWinExp = 11;  // 2^11 = 2048

enum class Dir : uint8_t { Left, Right, Up, Down };

// xorshift32 with unbiased bounded draws; seed it from the hardware RNG.
struct Rng {
  uint32_t s;
  uint32_t next();
  uint32_t below(uint32_t n);  // uniform 0..n-1 (n > 0)
};

// Cells hold exponents: 0 empty, 1 → 2, 2 → 4, … 11 → 2048. Trivially
// copyable: it is parked and saved as is.
struct Board {
  uint8_t cell[kCells];
  uint32_t score;
  uint8_t won;  // a 2048 tile was made in this game
  uint8_t pad[3];
};

inline uint32_t value(uint8_t exp) { return exp ? 1u << exp : 0; }

// One tile's way during a move: from → to (cell indices). `merged`: it is
// the second of a pair and disappears into the tile that lands on `to` too.
struct Slide {
  uint8_t from, to;
  bool merged;
};

struct Move {
  bool moved;     // anything changed (only then spawn a tile)
  bool justWon;   // this move made the first 2048
  uint32_t gain;  // score added
  uint8_t nSlides;
  Slide slides[kCells];  // every tile there was before the move
  uint8_t nMerged;
  uint8_t mergedAt[kCells / 2];  // cells holding a merged tile after it
};

void clear(Board &b);
void newGame(Board &b, Rng &rng);  // empty board, score 0, two tiles
Move slide(Board &b, Dir d);       // applies the move (score, won); no new tile
int spawn(Board &b, Rng &rng);     // 2 (90 %) or 4 (10 %) in a random empty cell; its index, -1 if full
bool over(const Board &b);         // no empty cell and no equal neighbours
int tiles(const Board &b);         // non-empty cells
uint8_t maxExp(const Board &b);

}  // namespace g2048
