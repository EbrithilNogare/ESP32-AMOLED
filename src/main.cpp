#include <Arduino.h>
#include <esp_chip_info.h>

#include "board.h"
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Placeholder app: prints what it runs on, then idles. Real work goes into
// modules under src/ (or lib/), initialised from setup() and serviced from loop().

static void printBootInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);

  Serial.printf("\n[boot] %s v%s (board rev V%d)\n", APP_NAME, APP_VERSION, BOARD_REVISION);
  Serial.printf("[boot] %s rev %d.%d, %d cores @ %lu MHz\n", ESP.getChipModel(),
                chip.revision / 100, chip.revision % 100, chip.cores,
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("[boot] flash %lu MB, PSRAM %lu KB (free %lu KB), heap free %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() >> 20),
                (unsigned long)(ESP.getPsramSize() >> 10),
                (unsigned long)(ESP.getFreePsram() >> 10),
                (unsigned long)(ESP.getFreeHeap() >> 10));
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < SERIAL_WAIT_MS;) delay(10);

  printBootInfo();
}

void loop() {
  delay(LOOP_IDLE_MS);
}
