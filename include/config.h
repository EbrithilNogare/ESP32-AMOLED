#pragma once

// ============================================================================
// App configuration — tunables that are safe to change and fine to commit.
// Hardware wiring lives in board.h, credentials in secrets.h.
// ============================================================================

#define APP_NAME "ESP32-AMOLED"
// tools/version.py passes the git-describe version; this is only the fallback.
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

#define SERIAL_BAUD 115200
// How long setup() waits for a USB serial monitor so the boot log isn't lost.
// Only on a cold boot: waking from deep sleep must stay fast.
#define SERIAL_WAIT_MS 1500

#ifndef APP_DEBUG
#define APP_DEBUG 0
#endif

// Main loop: the longest it sleeps between passes, and how often it polls the
// AXP2101 over I2C for PWR presses and charger changes.
#define LOOP_DELAY_MAX_MS 5
#define PMU_POLL_MS 50
#define BATTERY_REFRESH_MS 30000  // re-read the battery level this often

// POSIX TZ string for local time (here: Central Europe with DST).
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "pool.ntp.org"

// ---- display ----
#define LCD_SPI_HZ 40000000    // QSPI clock (vendor BSP value)
#define LCD_DRAW_BUF_LINES 40  // two partial buffers of 368 x 40 px
#define UI_FRAME_MS 16         // LVGL refresh period while active
#define UI_FRAME_MS_DIM 100    // ... while dimmed

// ---- power profiles (docs/ARCHITECTURE.md §3, §8) ----
// The active brightness is the user's setting (quick settings, saved in NVS).
// The 70 % ceiling also protects the AMOLED against burn-in.
#define BRIGHTNESS_USER_MIN_PCT 5
#define BRIGHTNESS_USER_MAX_PCT 70
#define BRIGHTNESS_USER_DEFAULT_PCT 60
// Dimmed: at most half the user's brightness, and at most these levels.
#define BRIGHTNESS_DIM_BATTERY 64   // 25 %
#define BRIGHTNESS_DIM_CHARGER 102  // 40 %
#define BRIGHTNESS_MINIMAL 38       // ~15 %: pomodoro minimal view
#define BRIGHTNESS_FADE_MS 300
#define BAT_IDLE_DIM_MS 10000
#define BAT_IDLE_BLACK_MS 15000
#define BAT_IDLE_SLEEP_MS 20000
#define CHG_IDLE_DIM_MS 60000
#define CHG_IDLE_BLACK_MS 300000  // plain black (display off)
// Minimal view: an app's redraw interval is clamped to this range (default
// when no app says). PWR reaches the ESP only through the AXP2101, which can't
// wake it from light sleep, so the CPU wakes at least every
// MINIMAL_PWR_POLL_MS just to poll the PMU: a PWR press is seen within ~1 s.
#define MINIMAL_REFRESH_DEFAULT_MS 1000
#define MINIMAL_REFRESH_MIN_MS 200
#define MINIMAL_REFRESH_MAX_MS 60000
#define MINIMAL_PWR_POLL_MS 1000

// ---- burn-in ----
#define PIXEL_SHIFT_PERIOD_MS 180000
#define PIXEL_SHIFT_MAX_PX 4

// ---- buttons ----
#define BUTTON_BACK_IS_BOOT 1  // 1: BOOT = back, PWR = home; 0: swapped
#define BUTTON_DEBOUNCE_MS 25
#define BUTTON_LONG_PRESS_MS \
  1000                              // long press → power-off animation → deep sleep
                                    // (also the AXP2101 PWR long press: 1000-2500 in 500 steps)
#define BUTTON_WAKE_IGNORE_MS 2000  // after a BOOT wake, a press still held this soon is ignored
#define POWER_OFF_ANIM_MS 1000
#define POWER_ON_ANIM_MS 600
#define RELEASE_WAIT_TIMEOUT_MS 3000  // sleep anyway if a button stays held this long

// ---- Wi-Fi ----
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define WIFI_RETRY_MS 15000
#define WIFI_JOIN_TIMEOUT_MS 15000  // a network picked on the device: first join, DHCP included
#define WIFI_GRACE_MS 30000         // radio stays on this long after the last release()

// ---- OTA (docs/ARCHITECTURE.md §12) ----
#define OTA_HOSTNAME "esp32-amoled"
#define OTA_MIN_BATTERY_PCT 30
#define OTA_HEALTHY_AFTER_MS 10000

// ---- sound ----
#define SOUND_SAMPLE_RATE 16000  // the ES8311 dividers in sound.cpp assume 16 kHz
// Volume 0-100 % (quick settings, saved in NVS), linear in dB on the ES8311
// DAC: 100 % = 0 dB, 1 % = -SOUND_VOLUME_RANGE_DB, 0 % = silent.
#define SOUND_VOLUME_DEFAULT_PCT 80  // -8 dB
#define SOUND_VOLUME_RANGE_DB 40
#define SOUND_ALARM_MS 30000  // a finished countdown beeps this long unless dismissed

// ---- home assistant ----
// Exactly 4 lights, in screen order: top-left, top-right, bottom-left, bottom-right.
#define HA_LIGHTS                                                                                     \
  {                                                                                                   \
    {"light.living_room", "Living room"}, {"light.kitchen", "Kitchen"}, {"light.bedroom", "Bedroom"}, \
        {"light.hallway", "Hallway"},                                                                 \
  }
#define HA_POLL_MS 3000
#define HA_HTTP_TIMEOUT_MS 1500

// ---- pomodoro ----
#define POMO_FOCUS_MIN 25
#define POMO_SHORT_BREAK_MIN 5
#define POMO_LONG_BREAK_MIN 15
#define POMO_SET_SIZE 4
#define POMO_AUTO_START_NEXT 1
#define POMO_SOUND 1

// ---- espresso ----
#define ESPRESSO_TARGET_MIN_S 25
#define ESPRESSO_TARGET_MAX_S 30
#define ESPRESSO_IDLE_DIM_MS 30000  // longer dim delay so the result stays readable

// ---- bot face ----
#define BOT_FACE_COLOR 0x00E5FF
#define BOT_FACE_FPS 30
#define BOT_FACE_FPS_DIM 10

// ---- dice (docs/apps/dice.md) ----
#define DICE_COLOR 0xFF8A1F  // primary colour: wireframes, kept roll, selected die
#define DICE_ROLL_MS 1150    // spin time of one die; later dice start 80 ms apart
#define DICE_FPS 30          // frame rate while dice are spinning

// ---- level (docs/apps/level.md) ----
#define LEVEL_COLOR 0xFFD54F     // app accent (menu, Hold, horizon line)
#define LEVEL_IDLE_DIM_MS 60000  // the screen stays bright longer: hands are busy
#define LEVEL_BEEP 1             // short beep when the reading becomes level
#define LEVEL_SAMPLE_MS 20       // accelerometer read period while the app is open

// ---- timer (docs/apps/timer.md) ----
#define TIMER_COLOR 0xB388FF           // app accent: lavender
#define TIMER_DEFAULT_COUNTDOWN_S 300  // first countdown before one is set: 5:00
// Brightness of the Timer's minimal views (the clock while presenting, a
// running countdown or stopwatch): readable across a room, still far below
// the active level. The digits move every minute against burn-in.
#define TIMER_MINIMAL_BRIGHTNESS 64  // 25 %
#define TIMER_ALARM_REPEAT_MS 2000   // a finished countdown beeps this often, for SOUND_ALARM_MS

// ---- noise (docs/apps/noise.md) ----
#define NOISE_COLOR 0x6E8BFF  // app accent: rain blue
#define NOISE_FPS 24          // animation frame rate
#define NOISE_FPS_DIM 8       // ... while dimmed
#define NOISE_FADE_MS 400     // sound fades in and out instead of clicking

// ---- IMU (QMI8658) axis mapping ----
// Raw sensor axis (0 = X, 1 = Y, 2 = Z) and sign for each screen axis: x to the
// right edge, y to the top edge, z out of the screen. Lying face up must read
// z ≈ +1 g. Not yet checked on the board: see docs/apps/level.md.
#define IMU_SCREEN_X_AXIS 0
#define IMU_SCREEN_X_SIGN 1
#define IMU_SCREEN_Y_AXIS 1
#define IMU_SCREEN_Y_SIGN 1
#define IMU_SCREEN_Z_AXIS 2
#define IMU_SCREEN_Z_SIGN 1
