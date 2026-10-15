#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DMA (1 << 3)
static inline void *heap_caps_malloc(size_t size, unsigned int caps) {
  (void)caps;
  return malloc(size);
}
static inline size_t heap_caps_get_free_size(unsigned int caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 7800000u : 217000u;  // plausible numbers for screenshots
}
