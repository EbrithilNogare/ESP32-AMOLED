// LVGL 9.6 configuration. Only settings that differ from LVGL's defaults are
// listed; everything else comes from lv_conf_internal.h. Rationale for the
// rendering setup: docs/UI_DESIGN.md §8.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

/* ---- colour & memory ---- */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565_SWAPPED  // what the QSPI panel takes

// LVGL's heap lives in PSRAM: widgets and styles don't need internal RAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (512U * 1024U)
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* ---- timing ---- */
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 330  // ~330 ppi panel

/* ---- OS ---- */
#define LV_USE_OS LV_OS_NONE  // only the UI task calls lv_* (ARCHITECTURE §6.3)

/* ---- software renderer: only the formats we draw ---- */
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1  // opacity/transform layers
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_COMPLEX 1  // rounded corners, arcs, anti-aliasing

/* ---- logging ---- */
#if APP_DEBUG
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#else
#define LV_USE_LOG 0
#endif

/* ---- fonts: generated ones (src/ui/generated), no built-in Montserrat ---- */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_body)
#define LV_FONT_DEFAULT &font_body

/* ---- themes: everything is styled by src/ui/theme ---- */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* ---- layouts ---- */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* ---- widgets we use ---- */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_IMAGE 1
#define LV_USE_ANIMIMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_SPINNER 1

/* ---- widgets we don't (saves flash) ---- */
#define LV_USE_ARCLABEL 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_OBSERVER 0

#endif /*LV_CONF_H*/
#endif
