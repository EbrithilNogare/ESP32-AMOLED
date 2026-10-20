// Hephy, the virtual pet (docs/apps/hephy.md): meters that sink slowly to a
// floor, forging trinkets in real time, the shelf, xp and levels. Everything
// runs on the wall clock (unix seconds) and is computed when the app opens:
// nothing needs the device awake. No LVGL, no Arduino.
#pragma once

#include <stdint.h>

namespace pet {

enum class Rarity : uint8_t { Common, Rare, Epic, Legendary };

struct Item {
  const char *name;
  Rarity rarity;
};

constexpr int kItems = 24;
constexpr uint8_t kNone = 0xFF;
const Item &item(int i);
uint32_t forgeSeconds(Rarity r);  // 1 h, 3 h, 6 h, 12 h
uint32_t newItemXp(Rarity r);     // 10, 25, 60, 150; a duplicate gives half

// Trivially copyable: saved to NVS as is. Bump kVersion when it changes.
constexpr uint16_t kVersion = 1;
struct State {
  uint16_t version;
  uint8_t forging;    // the item on the anvil, kNone
  uint8_t gift;       // a finished item waiting to be shown, kNone
  float energy, joy;  // 0..100
  uint32_t xp;
  uint32_t shelf;   // bit i: item i owned
  uint16_t forged;  // trinkets forged in all (duplicates too)
  uint16_t pad;
  uint32_t rng;
  int64_t lastSec;       // wall time of the last advance()
  int64_t forgeEndSec;   // when the forge is done (forging != kNone)
  int64_t lastSeenSec;   // the app was last open
  int64_t lastBonusSec;  // the last visit bonus
};

State fresh(int64_t nowSec, uint32_t seed);
bool valid(const State &s);  // version and ranges: false → use fresh()

// Brings the state up to `nowSec`: meters sink, a forge that is done
// becomes the gift. A clock that went backwards moves every stored time
// back with it (no decay, the forge's time left kept).
void advance(State &s, int64_t nowSec);

enum class Greeting : uint8_t { None, Hello, MissedYou, Asleep };
// The app opened (after advance()): how he greets you, and a visit bonus
// after a long enough time away.
Greeting visit(State &s, int64_t nowSec);
void seen(State &s, int64_t nowSec);  // the app is closing / the device sleeps

enum class Mood : uint8_t { Content, Happy, Sleepy, Lonely, Focused };
Mood mood(const State &s);

int level(uint32_t xp);            // 1..kMaxLevel
uint32_t levelStartXp(int level);  // xp where `level` begins
constexpr int kMaxLevel = 20;
int owned(const State &s);  // items on the shelf
bool has(const State &s, int i);

enum class Result : uint8_t { Ok, Full, Tired, Busy };
Result feed(State &s);  // +25 energy; Full at >= 90
void pet(State &s);     // +2 joy

// Strike: a tap's distance from the hot zone's centre, as a fraction of the
// zone's half width → 2 perfect (< 1/3), 1 good (<= 1), 0 miss.
uint8_t strikeScore(float offset);
constexpr int kStrikes = 5;
Result canStrike(const State &s);  // Tired under 10 energy
struct Round {
  uint8_t perfect, good;
  uint32_t xp;
  uint32_t forgeCutSec;  // taken off a running forge
};
Round finishRound(State &s, const uint8_t scores[kStrikes], int64_t nowSec);

Result canForge(const State &s);  // Busy (forging or a gift waiting), Tired under 25
// Starts forging (picks the item by the level's odds, a missing one if
// possible). kNone if it can't.
uint8_t startForge(State &s, int64_t nowSec);
int64_t forgeLeftSec(const State &s, int64_t nowSec);

struct Collected {
  uint8_t item;
  bool isNew;
  uint32_t xp;
};
Collected collect(State &s);  // takes the gift onto the shelf (item kNone if none)

}  // namespace pet
