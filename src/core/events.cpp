#include "events.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace core::events {

static QueueHandle_t queue;

void begin() {
  if (!queue) queue = xQueueCreate(32, sizeof(Event));
}

bool post(const Event &e) { return queue && xQueueSend(queue, &e, 0) == pdTRUE; }

bool postFromIsr(const Event &e) {
  BaseType_t woken = pdFALSE;
  bool ok = queue && xQueueSendFromISR(queue, &e, &woken) == pdTRUE;
  portYIELD_FROM_ISR(woken);
  return ok;
}

bool poll(Event &out) { return queue && xQueueReceive(queue, &out, 0) == pdTRUE; }

}  // namespace core::events
