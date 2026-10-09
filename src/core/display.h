#pragma once
#include <lvgl.h>
#include <stdint.h>

// The QSPI AMOLED (CO5300 on V2, SH8601 on V1) driven through ESP-IDF's
// esp_lcd SPI panel IO, plus the LVGL display that renders into it.
// Flushes are asynchronous DMA transfers, so LVGL renders the next chunk
// while the previous one is on the wire (docs/UI_DESIGN.md §8).
namespace core::display {

bool begin(uint16_t columnOffset);
lv_display_t *lv();

// Brightness is a panel command (0x51), there is no backlight pin.
void setBrightness(uint8_t level);
void fadeTo(uint8_t level, uint32_t ms);
uint8_t brightness();

void setPanelOn(bool on);  // 0x29 / 0x28: every pixel off, instant to undo
void enterSleep();         // brightness 0, display off, sleep-in (before deep sleep)
void setRefreshPeriod(uint32_t ms);
void pauseRendering(bool paused);
bool flushIdle();  // no DMA transfer in flight

}  // namespace core::display
