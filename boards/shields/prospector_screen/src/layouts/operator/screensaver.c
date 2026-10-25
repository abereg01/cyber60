#include "screensaver.h"

#include "display_colors.h"

// Image data lives in src/images/scoob.c (LVGL 9 RGB565 export, 280×240).
// We forward-declare by hand instead of using LV_IMAGE_DECLARE/LV_IMG_DECLARE
// — the ZMK-bundled LVGL doesn't expose LV_IMAGE_DECLARE in this header path,
// and the macro just expands to this extern anyway.
extern const lv_image_dsc_t scoob;

lv_obj_t *screensaver_screen_create(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    // Match the status-screen background so any image padding blends in if the
    // PNG isn't exactly 280×240 (it should be, but belt-and-suspenders).
    lv_obj_set_style_bg_color(screen, lv_color_hex(DISPLAY_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, &scoob);
    lv_obj_center(img);

    return screen;
}
