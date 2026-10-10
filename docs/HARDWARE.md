# Waveshare ESP32-S3-Touch-AMOLED-1.8 — hardware reference

Everything needed to write firmware for this board, collected in one place.
The pin numbers are mirrored in [`include/board.h`](../include/board.h).

- Product page: <https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm?sku=29957>
- Wiki: <https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8>
- Docs (new): <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8>
  ([FAQ](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8/FAQ),
  [Arduino setup](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino))
- Official demos (Arduino V1/V2 + ESP-IDF): <https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8>
- ESP-IDF BSP component: [`waveshare/esp32_s3_touch_amoled_1_8`](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8)

## Board revisions

There are two revisions. **Check the label on the back of the board.**
Waveshare switched shipments to V2 on 2026-05-30.

| | V1 | V2 |
| --- | --- | --- |
| AMOLED driver | SH8601 | CO5300 |
| Touch controller | FT3168 (I2C `0x38`) | CST820 (I2C `0x15`) |
| Display column offset | 0 | 16 |
| Arduino GFX class | `Arduino_SH8601` | `Arduino_CO5300(bus, rst, rot, w, h, 16, 0, 0, 0)` |
| Vendor Arduino examples | `examples/arduino` | `examples/arduino-v2` |

Pins are identical between revisions. Firmware can detect the revision at
runtime by probing the touch address on I2C (`0x15` → V2, `0x38` → V1) — the
vendor ESP-IDF BSP does exactly that, and so does this repo at boot
(`src/core/board_init.cpp`). `BOARD_REVISION` in `board.h` (default `2`) is
only the fallback when no touch controller answers.

> The vendor's V2 Arduino code still calls the touch chip `CST816x`/`CST816T`
> (same register interface as the CST820).

## Features (from the product page)

ESP32-S3-Touch-AMOLED-1.8 is a low-cost, high-performance MCU board designed by
Waveshare, tiny size, with onboard 1.8inch AMOLED display, power management IC,
6-axis sensor (3-axis accelerometer and 3-axis gyroscope), RTC, low power audio
codec, and so on, which makes it easy for you to develop and integrate it into
the products quickly.

- Equipped with ESP32-S3R8 Xtensa 32-bit LX7 dual-core processor, up to 240MHz main frequency
- Supports 2.4GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE), with onboard antenna
- Built in 512KB of SRAM and 384KB ROM, with onboard 8MB PSRAM and an external 16MB Flash memory
- Type-C connector, improving device compatibility, easier to use
- Onboard 1.8inch AMOLED display for clear color picture display, 368 × 448 resolution, 16.7M color
- Built-in CO5300 display driver and CST820 capacitive touch chip, using QSPI and I2C communication respectively, effectively saving the IO resources
- Onboard QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope) for detecting motion gesture, counting steps, etc.
- Onboard PCF85063 RTC chip, powered by main Lithium battery through AXP2101 chip, with reserved RTC battery pads for connecting a backup battery, ensuring RTC function during the replacement of the main battery
- Onboard PWR and BOOT programmable buttons for easy custom function development
- Onboard 3.7V MX1.25 Lithium battery recharge/discharge header, optional for 3.7V Lithium battery
- Reserved pads of 7 × GPIO, 1 × I2C, 1 × UART, and 1 × USB interface, for connecting peripherals and debugging
- Onboard TF card slot for extended storage and fast data transfer, suitable for applications such as data recording and media playback, simplifying circuit design
- Adopts AXP2101 IC for efficient power management, supports multiple voltage outputs, battery charging, battery management, and battery life optimization, etc.
- Adopts AMOLED screen, featuring advantages of high contrast, wide viewing angle, rich colors, fast response, thinner design, and low power consumption, etc.

## Specifications

| Item | Value |
| --- | --- |
| SoC | ESP32-S3R8, Xtensa LX7 dual-core, up to 240 MHz |
| SRAM / ROM | 512 KB / 384 KB |
| PSRAM | 8 MB, octal (OPI), in-package |
| Flash | 16 MB, external, QIO |
| Wireless | 2.4 GHz Wi-Fi 802.11 b/g/n, Bluetooth 5 LE, onboard antenna |
| Display | 1.8" AMOLED, 368 × 448, 16.7M colors, QSPI |
| Display area | 34.94 × 28.70 mm |
| Board size | 45.20 × 37.60 mm |
| Touch | capacitive, I2C, interrupt on GPIO21 |
| IMU | QMI8658, 3-axis accelerometer + 3-axis gyroscope |
| RTC | PCF85063A, backup battery pads |
| PMU | AXP2101 (rails, Li-ion charger, fuel gauge ADC, PWR key) |
| Audio | ES8311 codec, onboard SMD microphone, onboard speaker + amplifier |
| Storage | microSD (TF) slot, SDMMC 1-bit |
| IO expander | TCA9554 (I2C `0x20`) — LCD/touch reset, PWR key, PMU IRQ, SD |
| Buttons | BOOT (GPIO0), PWR (via AXP2101) |
| USB | USB-C, ESP32-S3 native USB (USB-Serial/JTAG) — flashing and serial |
| Battery | 3.7 V Li-ion/LiPo, MX1.25 connector; recommended 3.85 × 24 × 28 mm 400 mAh |
| Expansion pads | 7 × GPIO, 1 × I2C, 1 × UART, 1 × USB; 1.27 mm pitch; M2 standoffs |

## Pinout

### Display — QSPI AMOLED

| Signal | GPIO |
| --- | --- |
| CS | 12 |
| SCLK | 11 |
| SDIO0 (D0) | 4 |
| SDIO1 (D1) | 5 |
| SDIO2 (D2) | 6 |
| SDIO3 (D3) | 7 |
| RESET | via TCA9554 (EXIO0–2) |
| Backlight | none — AMOLED; brightness is panel command `0x51` (`gfx->setBrightness(0..255)`) |

### I2C bus (shared by every onboard chip)

| Signal | GPIO |
| --- | --- |
| SDA | 15 |
| SCL | 14 |
| Touch INT | 21 |

| Device | Address | Function |
| --- | --- | --- |
| CST820 (V2) | `0x15` | touch |
| ES8311 | `0x18` | audio codec control |
| TCA9554 | `0x20` | IO expander |
| AXP2101 | `0x34` | PMU |
| FT3168 (V1) | `0x38` | touch |
| PCF85063A | `0x51` | RTC |
| QMI8658 | `0x6B` | IMU (WHO_AM_I `0x00` = `0x05`) |

### TCA9554 IO expander

From the vendor examples (no board schematic is published yet):

| EXIO | Use |
| --- | --- |
| 0, 1, 2 | outputs — LCD and touch reset. Vendor init: set LOW, wait ~20 ms, set HIGH. An ESP restart alone does **not** reset the panel/touch. |
| 4 | input — HIGH while the PWR button is pressed (vendor `12_LVGL_AXP2101_ADC_Data`) |
| 5 | input — AXP2101 IRQ. It reaches only the expander, not an ESP GPIO, so a PWR press can't wake the chip from light or deep sleep (the firmware polls the PMU). |
| 7 | output — microSD D3/CS; drive HIGH before `SD_MMC.begin()` |

### Audio — ES8311 over I2S

| Signal | GPIO |
| --- | --- |
| MCLK | 16 |
| BCLK (SCLK) | 9 |
| WS (LRCK) | 45 |
| DOUT (ESP → codec, speaker) | 8 |
| DIN (codec → ESP, mic) | 10 |
| Speaker amp enable (PA) | 46, HIGH = on. Not an RTC pin: held low through deep sleep with `gpio_hold_en` + `gpio_deep_sleep_hold_en`, released in `sound::begin()`. |

### microSD — SDMMC 1-bit

| Signal | GPIO |
| --- | --- |
| CLK | 2 |
| CMD | 1 |
| D0 | 3 |

```cpp
SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
SD_MMC.begin("/sdcard", true /* 1-bit */);
```

### Buttons, USB, UART

| Function | GPIO |
| --- | --- |
| BOOT button | 0 (LOW = pressed; hold at power-on → download mode) |
| PWR button | AXP2101 PEK — short press IRQ via PMU; also EXIO4 |
| USB D− / D+ | 19 / 20 (native USB, USB-C) |
| UART0 TX / RX | 43 / 44 |

### Free GPIOs

The 7 GPIOs on the expansion pads are labelled on the silkscreen; the vendor
docs list no table. GPIOs not used by anything above (and not taken by the
flash/octal PSRAM on 26–37) are: 13, 17, 18, 38, 39, 40, 41, 42, 47, 48.
**Check the silkscreen before wiring.**

## Power

- **AXP2101** powers everything: it charges the battery from USB-C, switches
  between USB and battery, and owns the PWR button.
- **Power off:** hold PWR for ~6 s. **Power on again:** press PWR. Firmware can
  also shut the board down through the PMU (`XPowersLib`: `power.shutdown()`).
- The RTC is fed from the main battery through the AXP2101; solder a backup cell
  to the RTC pads to keep time while swapping the main battery.
- Battery voltage, charge state, VBUS, and die temperature are readable from
  the AXP2101 ADC (vendor example `12_LVGL_AXP2101_ADC_Data`).

## Flashing & recovery

- Plug USB-C and flash; the ESP32-S3 native USB auto-resets into the bootloader.
- If the port does not show up (e.g. firmware crashed USB, or deep sleep):
  hold **BOOT**, press/replug power, release BOOT, then flash.
- Factory images are in the vendor repo under `firmware/`
  (`...-FactoryOnly-*.bin` for V1, `...-V2-FactoryOnly-*.bin` for V2).

## Software

### PlatformIO (this repo)

`platformio.ini` uses the pioarduino platform (Arduino core 3.x) with board
`esp32-s3-devkitc1-n16r8` — same module: 16 MB QIO flash, 8 MB OPI PSRAM.
`ARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` to the USB-C port.

### Arduino IDE equivalent

The vendor CI builds with core **esp32 3.3.11** and FQBN
`esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`.

| Tools menu | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| USB CDC On Boot | Enabled (Serial over USB-C) |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

### Libraries

Versions bundled with the vendor Arduino examples (for reference; this repo
uses its own drivers, see [LIBRARIES.md](LIBRARIES.md)):

| Library | Version | For |
| --- | --- | --- |
| GFX Library for Arduino (moononournation) | 1.6.4 | `Arduino_ESP32QSPI` bus + `Arduino_SH8601` / `Arduino_CO5300` |
| SensorLib (lewisxhe) | 0.3.3 | QMI8658, PCF85063, touch drivers |
| XPowersLib (lewisxhe) | 0.2.6 | AXP2101 (`#define XPOWERS_CHIP_AXP2101`) |
| Adafruit XCA9554 | 1.0.0 | TCA9554 expander |
| Adafruit BusIO | 1.17.4 | dependency of XCA9554 |
| lvgl | 8.4.0 | UI (vendor ESP-IDF demos use LVGL v9) |
| Arduino_DriveBus | 1.0.1 | vendor touch/I2C helper — only in the vendor repo, not in the PlatformIO registry |

### ESP-IDF

ESP-IDF v5.5 / v6.0 with the managed BSP:

```yaml
# main/idf_component.yml
dependencies:
  idf: ">=5.5,<6.1"
  waveshare/esp32_s3_touch_amoled_1_8: "^2.0.3"
```

```
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

## Bring-up snippets (from the vendor examples)

Display (V2):

```cpp
#include <Arduino_GFX_Library.h>
Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK,
    PIN_LCD_SDIO0, PIN_LCD_SDIO1, PIN_LCD_SDIO2, PIN_LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0,
    LCD_WIDTH, LCD_HEIGHT, LCD_COL_OFFSET_CO5300, 0, 0, 0);
// V1: new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
gfx->begin();
gfx->setBrightness(255);
```

Reset display + touch through the expander (before `gfx->begin()`):

```cpp
#include <Wire.h>
#include <Adafruit_XCA9554.h>
Adafruit_XCA9554 expander;
Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
expander.begin(I2C_ADDR_TCA9554);
for (int p = 0; p <= 2; p++) { expander.pinMode(p, OUTPUT); expander.digitalWrite(p, LOW); }
delay(20);
for (int p = 0; p <= 2; p++) expander.digitalWrite(p, HIGH);
```

PMU, RTC, IMU:

```cpp
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
XPowersPMU power;
power.begin(Wire, I2C_ADDR_AXP2101, PIN_I2C_SDA, PIN_I2C_SCL);

#include <SensorPCF85063.hpp>
SensorPCF85063 rtc;
rtc.begin(Wire, PIN_I2C_SDA, PIN_I2C_SCL);

#include <SensorQMI8658.hpp>
SensorQMI8658 qmi;
qmi.begin(Wire, I2C_ADDR_QMI8658, PIN_I2C_SDA, PIN_I2C_SCL);
```

Speaker: `pinMode(PIN_PA_EN, OUTPUT); digitalWrite(PIN_PA_EN, HIGH);` then
configure the ES8311 over I2C and stream PCM over I2S. DAC volume register
`0x32`: 0.5 dB per step, `0xBF` = 0 dB, `0xFF` = +32 dB (clips), `0x00` =
mute; `SOUND_VOLUME` 0–100 maps linearly onto `0x00`–`0xBF`.

## Vendor demo index

Arduino (`examples/arduino`, V2 subset in `examples/arduino-v2`):
`01_HelloWorld`, `02_Drawing_board`, `03_GFX_AsciiTable`,
`04_GFX_FT3168_Image`, `05_GFX_PCF85063_simpleTime`, `06_GFX_ESPWiFiAnalyzer`,
`07_GFX_Clock`, `08_LVGL_Animation`, `09_LVGL_change_background`,
`10_LVGL_PCF85063_simpleTime`, `11_LVGL_QMI8658_ui`,
`12_LVGL_AXP2101_ADC_Data`, `13_LVGL_Widgets`, `14_LVGL_SD_Test`,
`15_ES8311`, `16_LVGL_Sqprj`.

ESP-IDF (`examples/esp-idf`): `00_board_check`, `00_bsp_quickstart`,
`01_project_template`, `02_hello_world`, `03_nvs_counter`,
`04_freertos_tasks`, `05_gpio_io`, `06_gpio_interrupt`, `08_i2c_tools`,
`09_sdmmc`, `10_wifi_station`, `12_i2s_codec`, `13_display_colorbar`,
`14_lvgl_demo_v9`, `90_axp2101_pmu`, `91_pcf85063_rtc`, `92_qmi8658_imu`.
