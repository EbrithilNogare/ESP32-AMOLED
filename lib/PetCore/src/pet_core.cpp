#include "pet_core.h"

namespace pet {

namespace {

const Item kTable[kItems] = {
    {"Key", Rarity::Common},     {"Anchor", Rarity::Common},        {"Cog", Rarity::Common},
    {"Bell", Rarity::Common},    {"Hourglass", Rarity::Common},     {"Lamp", Rarity::Common},
    {"Kettle", Rarity::Common},  {"Wrench", Rarity::Common},        {"Coin", Rarity::Common},
    {"Bolt", Rarity::Common},    {"Shield", Rarity::Rare},          {"Crossed swords", Rarity::Rare},
    {"Knight", Rarity::Rare},    {"Compass", Rarity::Rare},         {"Pocket watch", Rarity::Rare},
    {"Rocket", Rarity::Rare},    {"Castle", Rarity::Rare},          {"Diamond", Rarity::Epic},
    {"Crown", Rarity::Epic},     {"Medal", Rarity::Epic},           {"Trophy", Rarity::Epic},
    {"Star wand", Rarity::Epic}, {"Golden owl", Rarity::Legendary}, {"Sun spark", Rarity::Legendary},
};

// Meters sink to a floor, never below (docs/apps/hephy.md, Rules).
constexpr float kEnergyFloor = 15, kJoyFloor = 20;
constexpr float kEnergyPerSec = 85.0f / (72 * 3600);
constexpr float kJoyPerSec = 80.0f / (48 * 3600);
constexpr float kFeed = 25, kFullAt = 90, kStrikeCost = 5, kStrikeMin = 10, kForgeCost = 25;
constexpr float kPet = 2, kVisitJoy = 15;
constexpr uint32_t kVisitXp = 3;
constexpr int64_t kHelloSec = 3600, kMissedSec = 12 * 3600, kAsleepSec = 24 * 3600, kBonusSec = 4 * 3600;
constexpr uint32_t kPerfectCutSec = 10 * 60;

float clamp100(float v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

// A meter sinks by `drop`, but not below the floor (and one already under
// the floor stays where it is).
float sink(float v, float drop, float floor) {
  if (v <= floor) return v;
  v -= drop;
  return v < floor ? floor : v;
}

uint32_t next(State &s) {
  uint32_t x = s.rng ? s.rng : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return s.rng = x;
}

uint32_t below(State &s, uint32_t n) { return next(s) % n; }

}  // namespace

const Item &item(int i) { return kTable[i >= 0 && i < kItems ? i : 0]; }

uint32_t forgeSeconds(Rarity r) {
  switch (r) {
    case Rarity::Common: return 1 * 3600;
    case Rarity::Rare: return 3 * 3600;
    case Rarity::Epic: return 6 * 3600;
    case Rarity::Legendary: return 12 * 3600;
  }
  return 3600;
}

uint32_t newItemXp(Rarity r) {
  switch (r) {
    case Rarity::Common: return 10;
    case Rarity::Rare: return 25;
    case Rarity::Epic: return 60;
    case Rarity::Legendary: return 150;
  }
  return 10;
}

State fresh(int64_t nowSec, uint32_t seed) {
  State s{};
  s.version = kVersion;
  s.forging = s.gift = kNone;
  s.energy = 80;
  s.joy = 70;
  s.rng = seed ? seed : 1;
  s.lastSec = s.lastSeenSec = nowSec;
  s.lastBonusSec = nowSec;
  return s;
}

bool valid(const State &s) {
  if (s.version != kVersion) return false;
  if (!(s.energy >= 0 && s.energy <= 100) || !(s.joy >= 0 && s.joy <= 100)) return false;  // also NaN
  if (s.forging != kNone && s.forging >= kItems) return false;
  if (s.gift != kNone && s.gift >= kItems) return false;
  return true;
}

void advance(State &s, int64_t nowSec) {
  const int64_t dt = nowSec - s.lastSec;
  if (dt < 0) {  // the clock went back: move everything with it
    s.forgeEndSec += dt;
    s.lastSeenSec += dt;
    s.lastBonusSec += dt;
    s.lastSec = nowSec;
    return;
  }
  s.energy = sink(s.energy, float(dt) * kEnergyPerSec, kEnergyFloor);
  s.joy = sink(s.joy, float(dt) * kJoyPerSec, kJoyFloor);
  s.lastSec = nowSec;
  if (s.forging != kNone && nowSec >= s.forgeEndSec) {
    s.gift = s.forging;
    s.forging = kNone;
  }
}

Greeting visit(State &s, int64_t nowSec) {
  const int64_t away = nowSec - s.lastSeenSec;
  if (nowSec - s.lastBonusSec >= kBonusSec) {
    s.joy = clamp100(s.joy + kVisitJoy);
    s.xp += kVisitXp;
    s.lastBonusSec = nowSec;
  }
  s.lastSeenSec = nowSec;
  if (away > kAsleepSec) return Greeting::Asleep;
  if (away > kMissedSec) return Greeting::MissedYou;
  if (away > kHelloSec) return Greeting::Hello;
  return Greeting::None;
}

void seen(State &s, int64_t nowSec) {
  if (nowSec > s.lastSeenSec) s.lastSeenSec = nowSec;
}

Mood mood(const State &s) {
  if (s.forging != kNone) return Mood::Focused;
  if (s.energy < 30) return Mood::Sleepy;
  if (s.joy < 35) return Mood::Lonely;
  if (s.joy >= 75 && s.energy >= 50) return Mood::Happy;
  return Mood::Content;
}

uint32_t levelStartXp(int level) {
  if (level <= 1) return 0;
  return 25u * uint32_t(level) * uint32_t(level - 1) / 2;
}

int level(uint32_t xp) {
  int l = 1;
  while (l < kMaxLevel && xp >= levelStartXp(l + 1)) l++;
  return l;
}

bool has(const State &s, int i) { return i >= 0 && i < kItems && (s.shelf >> i & 1u); }

int owned(const State &s) {
  int n = 0;
  for (int i = 0; i < kItems; i++) n += has(s, i);
  return n;
}

Result feed(State &s) {
  if (s.energy >= kFullAt) return Result::Full;
  s.energy = clamp100(s.energy + kFeed);
  s.xp += 2;
  return Result::Ok;
}

void pet(State &s) { s.joy = clamp100(s.joy + kPet); }

uint8_t strikeScore(float offset) {
  const float d = offset < 0 ? -offset : offset;
  return d < 1.0f / 3 ? 2 : d <= 1.0f ? 1 : 0;
}

Result canStrike(const State &s) { return s.energy < kStrikeMin ? Result::Tired : Result::Ok; }

Round finishRound(State &s, const uint8_t scores[kStrikes], int64_t nowSec) {
  Round r{};
  for (int i = 0; i < kStrikes; i++) {
    r.perfect += scores[i] == 2;
    r.good += scores[i] == 1;
    r.xp += scores[i];
  }
  s.joy = clamp100(s.joy + 4.0f * r.perfect + 2.0f * r.good);
  s.energy = clamp100(s.energy - kStrikeCost);
  s.xp += r.xp;
  if (s.forging != kNone && r.perfect) {
    const int64_t left = s.forgeEndSec - nowSec;
    const int64_t cut = int64_t(r.perfect) * kPerfectCutSec;
    r.forgeCutSec = uint32_t(left <= 0 ? 0 : left < cut ? left : cut);
    s.forgeEndSec -= r.forgeCutSec;
    advance(s, nowSec);  // done now? it becomes the gift
  }
  return r;
}

Result canForge(const State &s) {
  if (s.forging != kNone || s.gift != kNone) return Result::Busy;
  if (s.energy < kForgeCost) return Result::Tired;
  return Result::Ok;
}

uint8_t startForge(State &s, int64_t nowSec) {
  if (canForge(s) != Result::Ok) return kNone;
  // Odds per level band (common, rare, epic, legendary), in percent.
  static const uint8_t kOdds[3][4] = {{75, 22, 3, 0}, {60, 28, 10, 2}, {45, 32, 17, 6}};
  const int lv = level(s.xp);
  const uint8_t *odds = kOdds[lv >= 6 ? 2 : lv >= 3 ? 1 : 0];
  uint32_t roll = below(s, 100);
  int rarity = 0;
  while (rarity < 3 && roll >= odds[rarity]) roll -= odds[rarity++];
  // A missing item of that rarity if there is one, else any of it.
  uint8_t pool[kItems], missing[kItems];
  int nPool = 0, nMissing = 0;
  for (int i = 0; i < kItems; i++) {
    if (int(kTable[i].rarity) != rarity) continue;
    pool[nPool++] = uint8_t(i);
    if (!has(s, i)) missing[nMissing++] = uint8_t(i);
  }
  const uint8_t it = nMissing ? missing[below(s, uint32_t(nMissing))] : pool[below(s, uint32_t(nPool))];
  s.energy = clamp100(s.energy - kForgeCost);
  s.forging = it;
  s.forgeEndSec = nowSec + forgeSeconds(kTable[it].rarity);
  return it;
}

int64_t forgeLeftSec(const State &s, int64_t nowSec) {
  if (s.forging == kNone) return 0;
  const int64_t left = s.forgeEndSec - nowSec;
  return left > 0 ? left : 0;
}

Collected collect(State &s) {
  Collected c{kNone, false, 0};
  if (s.gift == kNone) return c;
  c.item = s.gift;
  c.isNew = !has(s, c.item);
  const uint32_t xp = newItemXp(kTable[c.item].rarity);
  c.xp = c.isNew ? xp : xp / 2;
  s.shelf |= 1u << c.item;
  s.xp += c.xp;
  if (s.forged < UINT16_MAX) s.forged++;
  s.gift = kNone;
  return c;
}

}  // namespace pet
