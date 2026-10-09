#pragma once

// ============================================================================
// App configuration — tunables that are safe to change and fine to commit.
// Hardware wiring lives in board.h, credentials in secrets.h.
// ============================================================================

#define APP_NAME          "ESP32-AMOLED"
#define APP_VERSION       "0.1.0"

#define SERIAL_BAUD       115200
// How long setup() waits for a USB serial monitor to attach so the boot log
// isn't lost. 0 = don't wait.
#define SERIAL_WAIT_MS    2000

// Main loop idle period. Keep the loop from busy-spinning while nothing runs.
#define LOOP_IDLE_MS      1000

#ifndef APP_DEBUG
#define APP_DEBUG         0
#endif
