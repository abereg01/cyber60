#pragma once

#include <lvgl.h>

// Create a fresh LVGL screen object showing the full-screen idle image
// (Scoob, 280×240 RGB565). Returned screen is detached — caller passes it to
// lv_screen_load() when ZMK reports activity state == ZMK_ACTIVITY_IDLE.
lv_obj_t *screensaver_screen_create(void);
