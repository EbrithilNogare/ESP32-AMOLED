#pragma once

// ============================================================================
// App configuration — tunables that are safe to change and fine to commit.
// Hardware wiring lives in board.h, credentials in secrets.h.
// ============================================================================

#define APP_NAME          "ESP32-AMOLED"
// tools/version.py passes the git-describe version; this is only the fallback.
#ifndef APP_VERSION
#define APP_VERSION       "0.0.0-dev"
#endif

#define SERIAL_BAUD       115200
// How long setup() waits for a USB serial monitor so the boot log isn't lost.
// Only on a cold boot: waking from deep sleep must stay fast.
#define SERIAL_WAIT_MS    1500

#ifndef APP_DEBUG
#define APP_DEBUG         0
#endif

// POSIX TZ string for local time (here: Central Europe with DST).
#define TIMEZONE          "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER        "pool.ntp.org"

// ---- display ----
#define LCD_SPI_HZ            40000000   // QSPI clock (vendor BSP value)
#define LCD_DRAW_BUF_LINES    40         // two partial buffers of 368 x 40 px
#define UI_FRAME_MS           16         // LVGL refresh period while active
#define UI_FRAME_MS_DIM       100        // ... while dimmed

// ---- power profiles (docs/ARCHITECTURE.md §3, §8) ----
#define BRIGHTNESS_MAX_BATTERY     255
#define BRIGHTNESS_MAX_CHARGER     204   // 80 %: burn-in protection
#define BRIGHTNESS_DIM_BATTERY     64    // 25 %
#define BRIGHTNESS_DIM_CHARGER     102   // 40 %
#define BRIGHTNESS_MINIMAL         38    // ~15 %: pomodoro minimal view
#define BRIGHTNESS_FADE_MS         300
#define BAT_IDLE_DIM_MS            10000
#define BAT_IDLE_BLACK_MS          15000
#define BAT_IDLE_SLEEP_MS          20000
#define CHG_IDLE_DIM_MS            60000
#define CHG_IDLE_BLACK_MS          300000   // plain black (display off)

// ---- burn-in ----
#define PIXEL_SHIFT_PERIOD_MS      180000
#define PIXEL_SHIFT_MAX_PX         4

// ---- buttons ----
#define BUTTON_BACK_IS_BOOT        1     // 1: BOOT = back, PWR = home; 0: swapped
#define BUTTON_DEBOUNCE_MS         25
#define BUTTON_LONG_PRESS_MS       1000  // long press → power-off animation → deep sleep
#define POWER_OFF_ANIM_MS          1000
#define POWER_ON_ANIM_MS           600
#define RELEASE_WAIT_TIMEOUT_MS    3000  // sleep anyway if a button stays held this long

// ---- Wi-Fi ----
#define WIFI_CONNECT_TIMEOUT_MS    8000
#define WIFI_RETRY_MS              15000

// ---- OTA (docs/ARCHITECTURE.md §12) ----
#define OTA_HOSTNAME               "esp32-amoled"
#define OTA_MIN_BATTERY_PCT        30
#define OTA_HEALTHY_AFTER_MS       10000

// ---- sound ----
#define SOUND_SAMPLE_RATE          16000
#define SOUND_VOLUME               70    // 0-100

// ---- home assistant ----
// Exactly 4 lights, in screen order: top-left, top-right, bottom-left, bottom-right.
#define HA_LIGHTS { \
    {"light.living_room", "Living room"}, \
    {"light.kitchen",     "Kitchen"},     \
    {"light.bedroom",     "Bedroom"},     \
    {"light.hallway",     "Hallway"},     \
}
#define HA_POLL_MS            3000
#define HA_HTTP_TIMEOUT_MS    1500

// ---- pomodoro ----
#define POMO_FOCUS_MIN          25
#define POMO_SHORT_BREAK_MIN    5
#define POMO_LONG_BREAK_MIN     15
#define POMO_SET_SIZE           4
#define POMO_AUTO_START_NEXT    1
#define POMO_SOUND              1

// ---- espresso ----
#define ESPRESSO_TARGET_MIN_S   25
#define ESPRESSO_TARGET_MAX_S   30
#define ESPRESSO_IDLE_DIM_MS    30000   // longer dim delay so the result stays readable

// ---- bot face ----
#define BOT_FACE_COLOR          0x00E5FF
#define BOT_FACE_FPS            30
#define BOT_FACE_FPS_DIM        10
