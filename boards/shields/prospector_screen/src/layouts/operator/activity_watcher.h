#pragma once

#include <lvgl.h>

// Wire up the activity-state listener and remember the two screens it swaps
// between. Call once from zmk_display_status_screen() AFTER both screens are
// created. The listener fires on every transition reported by ZMK:
//   ZMK_ACTIVITY_ACTIVE → load status_screen
//   ZMK_ACTIVITY_IDLE   → load screensaver_screen
//   ZMK_ACTIVITY_SLEEP  → load status_screen (display blanks separately)
//
// Idle threshold is CONFIG_ZMK_IDLE_TIMEOUT (default 30000 ms).
void activity_watcher_setup(lv_obj_t *status_screen, lv_obj_t *screensaver_screen);
