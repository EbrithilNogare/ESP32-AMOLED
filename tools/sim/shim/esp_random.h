#pragma once
#include <stdint.h>
#include <stdlib.h>
// rand() is only 31 bits, so combine two calls. Two statements: the order of
// evaluation of operands within one expression is unspecified.
inline uint32_t esp_random() {
  const uint32_t hi = uint32_t(rand());
  const uint32_t lo = uint32_t(rand());
  return hi << 16 ^ lo;
}
