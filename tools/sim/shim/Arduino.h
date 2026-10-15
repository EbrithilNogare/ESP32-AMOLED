// Host shim for the UI simulator: just enough of Arduino for the apps.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

using std::max;
using std::min;

namespace sim {
extern uint32_t nowMs;
}
inline uint32_t millis() { return sim::nowMs; }
inline void delay(uint32_t) {}

struct SimEsp {
  void restart() { printf("[sim] ESP.restart()\n"); }
};
extern SimEsp ESP;
