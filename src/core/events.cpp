#include "events.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace core::events {

static constexpr UBaseType_t kQueueDepth = 32;  // drained every loop pass; bursts are a few events
static QueueHandle_t queue;

void begin() {
  if (!queue) queue = xQueueCreate(kQueueDepth, sizeof(Event));
}

bool post(const Event &e) { return queue && xQueueSend(queue, &e, 0) == pdTRUE; }

bool poll(Event &out) { return queue && xQueueReceive(queue, &out, 0) == pdTRUE; }

}  // namespace core::events
