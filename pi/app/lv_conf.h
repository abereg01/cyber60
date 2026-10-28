/* LVGL config for the Pi desk screen, used by the Pi build and by
 * pi/tools/preview.sh (which defines DECK_PREVIEW). 16-bit colour like the
 * Prospector, software renderer, no theme; fonts come from TTF files at
 * runtime (Tiny TTF), so the layout can use any size, and the dog from a PNG.
 * Everything else is LVGL's default. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_DEF_REFR_PERIOD 16 // 60 fps
#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0 // stdout carries the preview's video frames
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_TILEVIEW 1
#define LV_USE_KEYBOARD 1
#define LV_USE_TEXTAREA 1

#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256
#define LV_USE_LODEPNG 1 // the dog's sprite sheet
/* The touch keyboard's ⌫ ✓ keys are LVGL symbols, which only the built-in
 * Montserrat has: the UI font falls back to it. */
#define LV_FONT_MONTSERRAT_22 1

/* Pi: DRM (double-buffered, no tearing) for the Touch Display, the
 * framebuffer when the picture must be turned 180°, evdev for touch. */
#ifndef DECK_PREVIEW
#define LV_USE_LINUX_DRM 1
#endif
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_USE_EVDEV 1

#endif
