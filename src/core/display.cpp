// Panel command layer derived from Espressif's esp_lcd_sh8601 driver
// (Apache-2.0, https://github.com/waveshareteam/Waveshare-ESP32-components),
// with the init sequence from Waveshare's esp32_s3_touch_amoled_1_8 BSP.
// SH8601 and CO5300 share the QSPI protocol, so one sequence covers V1 and V2;
// only the column offset differs.
#include "display.h"

#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>

#include "board.h"
#include "config.h"

namespace core::display {

static const char *TAG = "display";

// QSPI framing: one opcode byte, then the DCS command in the address phase.
static constexpr uint32_t OPCODE_WRITE_CMD = 0x02;
static constexpr uint32_t OPCODE_WRITE_COLOR = 0x32;

struct InitCmd {
  uint8_t cmd;
  uint8_t data[4];
  uint8_t len;
  uint16_t delayMs;
};

// Vendor BSP sequence, except brightness (0x51) starts at 0 so the panel
// doesn't light up showing whatever its RAM held before the first frame.
static const InitCmd kInit[] = {
    {0xFE, {0x00}, 1, 0}, {0xC4, {0x80}, 1, 0}, {0x3A, {0x55}, 1, 0},  // RGB565
    {0x35, {0x00}, 1, 0},                                              // tearing effect line on
    {0x53, {0x20}, 1, 0},                                              // brightness control on
    {0x51, {0x00}, 1, 0}, {0x63, {0xFF}, 1, 0}, {0x11, {0}, 0, 120},   // sleep out
    {0x29, {0}, 0, 0},                                                 // display on
};

static esp_lcd_panel_io_handle_t io;
static lv_display_t *disp;
static uint16_t xGap;
static uint8_t level;
static volatile bool flushing;

static esp_err_t txParam(uint8_t cmd, const void *data, size_t len) {
  return esp_lcd_panel_io_tx_param(io, (int)((OPCODE_WRITE_CMD << 24) | (uint32_t(cmd) << 8)), data, len);
}

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
  flushing = false;
  lv_display_flush_ready(disp);
  return false;
}

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const uint16_t x1 = a->x1 + xGap, x2 = a->x2 + xGap;
  const uint8_t caset[4] = {uint8_t(x1 >> 8), uint8_t(x1), uint8_t(x2 >> 8), uint8_t(x2)};
  const uint8_t raset[4] = {uint8_t(a->y1 >> 8), uint8_t(a->y1), uint8_t(a->y2 >> 8), uint8_t(a->y2)};
  // tx_param waits for the previous colour transfer, so the window commands
  // can't overtake pixels still on the wire.
  txParam(0x2A, caset, 4);
  txParam(0x2B, raset, 4);
  flushing = true;
  const size_t bytes = size_t(lv_area_get_width(a)) * lv_area_get_height(a) * 2;
  esp_lcd_panel_io_tx_color(io, (int)((OPCODE_WRITE_COLOR << 24) | (0x2C << 8)), px, bytes);
  (void)d;
}

// CO5300/SH8601 need even-aligned windows: start on an even pixel, end on odd.
static void rounderCb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

bool begin(uint16_t columnOffset) {
  xGap = columnOffset;
  const size_t bufPixels = size_t(LCD_WIDTH) * LCD_DRAW_BUF_LINES;
  const size_t bufBytes = bufPixels * 2;

  spi_bus_config_t bus = {};
  bus.sclk_io_num = PIN_LCD_SCLK;
  bus.data0_io_num = PIN_LCD_SDIO0;
  bus.data1_io_num = PIN_LCD_SDIO1;
  bus.data2_io_num = PIN_LCD_SDIO2;
  bus.data3_io_num = PIN_LCD_SDIO3;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = bufBytes + 64;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed");
    return false;
  }

  esp_lcd_panel_io_spi_config_t cfg = {};
  cfg.cs_gpio_num = PIN_LCD_CS;
  cfg.dc_gpio_num = -1;
  cfg.spi_mode = 0;
  cfg.pclk_hz = LCD_SPI_HZ;
  cfg.trans_queue_depth = 10;
  cfg.on_color_trans_done = onColorDone;
  cfg.lcd_cmd_bits = 32;
  cfg.lcd_param_bits = 8;
  cfg.flags.quad_mode = 1;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io) != ESP_OK) {
    ESP_LOGE(TAG, "panel IO init failed");
    return false;
  }

  const uint8_t madctl = 0x00, colmod = 0x55;
  txParam(0x36, &madctl, 1);
  txParam(0x3A, &colmod, 1);
  for (const InitCmd &c : kInit) {
    txParam(c.cmd, c.len ? c.data : nullptr, c.len);
    if (c.delayMs) delay(c.delayMs);
  }

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  // Draw buffers stay in internal DMA RAM: small, on the hot path, and
  // rendering into PSRAM is noticeably slower.
  void *b1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b1 || !b2) {
    ESP_LOGE(TAG, "no DMA RAM for draw buffers");
    return false;
  }
  lv_display_set_buffers(disp, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_add_event_cb(disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);
  ESP_LOGI(TAG, "%dx%d QSPI @ %d MHz, column offset %u", LCD_WIDTH, LCD_HEIGHT, LCD_SPI_HZ / 1000000, xGap);
  return true;
}

lv_display_t *lv() { return disp; }

void setBrightness(uint8_t v) {
  level = v;
  txParam(0x51, &v, 1);
}

uint8_t brightness() { return level; }

static void fadeExec(void *, int32_t v) { setBrightness(uint8_t(v)); }

void fadeTo(uint8_t target, uint32_t ms) {
  lv_anim_delete(&level, nullptr);
  if (ms == 0 || target == level) {
    setBrightness(target);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &level);
  lv_anim_set_values(&a, level, target);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_exec_cb(&a, fadeExec);
  lv_anim_start(&a);
}

void setPanelOn(bool on) { txParam(on ? 0x29 : 0x28, nullptr, 0); }

void enterSleep() {
  lv_anim_delete(&level, nullptr);
  setBrightness(0);
  setPanelOn(false);
  txParam(0x10, nullptr, 0);  // sleep in
  delay(120);
}

void setRefreshPeriod(uint32_t ms) {
  lv_timer_t *t = lv_display_get_refr_timer(disp);
  if (t) lv_timer_set_period(t, ms);
}

void pauseRendering(bool paused) {
  lv_timer_t *t = lv_display_get_refr_timer(disp);
  if (!t) return;
  if (paused)
    lv_timer_pause(t);
  else
    lv_timer_resume(t);
}

bool flushIdle() { return !flushing; }

}  // namespace core::display
