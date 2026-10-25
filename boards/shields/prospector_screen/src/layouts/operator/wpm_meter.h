#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define WPM_BAR_COUNT 26
// 60 WPM ≈ 17/26 bars (~65%) with WPM_MAX=90, which feels right for the
// typical typing range. Was 120 (60 WPM showed half-full).
#define WPM_MAX 90

struct zmk_widget_wpm_meter {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *bars[WPM_BAR_COUNT];
    lv_obj_t *peak_indicator;
    lv_obj_t *wpm_label;
    lv_obj_t *layer_label;
};

int zmk_widget_wpm_meter_init(struct zmk_widget_wpm_meter *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_wpm_meter_obj(struct zmk_widget_wpm_meter *widget);
