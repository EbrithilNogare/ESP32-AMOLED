#pragma once

// ============================================================================
// Waveshare ESP32-S3-Touch-AMOLED-1.8 — fixed hardware wiring.
// Taken from Waveshare's official pin_config.h and ESP-IDF BSP
// (waveshare/esp32_s3_touch_amoled_1_8). These are traces on the PCB:
// never change them. App-level tunables live in config.h.
// Full description: docs/HARDWARE.md
// ============================================================================

// ---------------- board revision ----------------
// The revision is printed on the label on the back of the board.
//   V1: SH8601 AMOLED driver + FT3168 touch (I2C 0x38)
//   V2: CO5300 AMOLED driver + CST820 touch (I2C 0x15), shipping since 2026-05-30
// Firmware can also tell them apart at runtime by probing the touch address.
#ifndef BOARD_REVISION
#define BOARD_REVISION 2
#endif

// ---------------- display: 1.8" AMOLED, 368x448, QSPI ----------------
#define PIN_LCD_CS 12
#define PIN_LCD_SCLK 11
#define PIN_LCD_SDIO0 4
#define PIN_LCD_SDIO1 5
#define PIN_LCD_SDIO2 6
#define PIN_LCD_SDIO3 7
// LCD reset is not on a GPIO — it goes through the IO expander (EXIO, below).
// Brightness is a panel command (0x51), there is no backlight pin.
#define LCD_WIDTH 368
#define LCD_HEIGHT 448
// Column offset of the visible area in the driver RAM, per panel driver
// (the revision is detected at runtime, so both are needed).
#define LCD_COL_OFFSET_CO5300 16  // V2: the panel sits 16 columns into the driver RAM
#define LCD_COL_OFFSET_SH8601 0   // V1

// ---------------- shared I2C bus (touch, PMU, RTC, IMU, codec, expander) ----------------
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 14
#define PIN_TOUCH_INT 21
#define I2C_FREQ_HZ 400000  // every device on the bus supports fast mode
#define I2C_TIMEOUT_MS 20

#define I2C_ADDR_TOUCH_FT3168 0x38  // V1
#define I2C_ADDR_TOUCH_CST820 0x15  // V2
#define I2C_ADDR_ES8311 0x18        // audio codec
#define I2C_ADDR_TCA9554 0x20       // IO expander
#define I2C_ADDR_AXP2101 0x34       // power management
#define I2C_ADDR_PCF85063 0x51      // RTC
#define I2C_ADDR_QMI8658 0x6B       // IMU

// ---------------- TCA9554 IO expander pins (EXIO0..7) ----------------
// From the vendor examples: EXIO0-2 are pulsed LOW->HIGH at boot to reset the
// display and touch (an ESP restart alone does not reset them).
#define EXIO_LCD_TOUCH_RST0 0
#define EXIO_LCD_TOUCH_RST1 1
#define EXIO_LCD_TOUCH_RST2 2
#define EXIO_PWR_KEY 4  // input, HIGH while PWR is pressed (vendor 12_LVGL_AXP2101 demo)
#define EXIO_PMU_IRQ 5  // input, AXP2101 interrupt (no ESP GPIO known to carry it: it can't wake the chip)
#define EXIO_SD_D3 7    // microSD D3/CS: drive HIGH before SD_MMC.begin() (keeps SD mode)

// ---------------- audio: ES8311 codec + speaker amp + mic ----------------
#define PIN_I2S_MCLK 16
#define PIN_I2S_BCLK 9
#define PIN_I2S_WS 45   // LRCK
#define PIN_I2S_DOUT 8  // ESP -> codec (speaker)
#define PIN_I2S_DIN 10  // codec -> ESP (microphone)
#define PIN_PA_EN 46    // speaker amplifier enable, HIGH = on

// ---------------- microSD (SDMMC, 1-bit) ----------------
#define PIN_SD_CLK 2
#define PIN_SD_CMD 1
#define PIN_SD_D0 3

// ---------------- buttons ----------------
#define PIN_BOOT_BUTTON 0  // LOW when pressed; hold at power-on for download mode
// PWR is wired to the AXP2101 (power on/off); read presses via the PMU IRQ
// (isPekeyShortPressIrq) or EXIO_PWR_KEY.

// ---------------- USB / UART ----------------
// USB-C is the ESP32-S3 native USB (GPIO19 D-, GPIO20 D+): flashing + Serial.
#define PIN_UART0_TX 43
#define PIN_UART0_RX 44
