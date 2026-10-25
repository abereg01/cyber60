#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

// One bar for the Cyber60 (the only peripheral): its length is the battery
// level, its colour the connection state, with the percentage next to it.
struct zmk_widget_split_indicator {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *track;
    lv_obj_t *fill;
    lv_obj_t *label;
};

int zmk_widget_split_indicator_init(struct zmk_widget_split_indicator *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_split_indicator_obj(struct zmk_widget_split_indicator *widget);
