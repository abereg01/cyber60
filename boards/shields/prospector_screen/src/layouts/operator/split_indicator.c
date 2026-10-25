#include "split_indicator.h"

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_central_status_changed.h>

#include <fonts.h>

#include "display_colors.h"

// Cyber60 status bar: replaces KLOR's two half-bars. The fill length is the
// keyboard's battery level; blue when connected (orange at 20% or less), grey
// when disconnected. The label shows the percentage, or "--" while
// disconnected or before the first battery report.

#define BAR_WIDTH 210
#define BAR_HEIGHT 4
#define LOW_BATTERY 20

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct split_indicator_state {
    bool connected;
    int8_t level; // -1 = unknown
};

// Cached state; each event only carries one of the two fields.
static bool keyboard_connected = false;
static int8_t battery_level = -1;

static void split_indicator_update_cb(struct split_indicator_state state) {
    struct zmk_widget_split_indicator *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        uint32_t color = !state.connected ? DISPLAY_COLOR_MOD_INACTIVE
                         : (state.level >= 0 && state.level <= LOW_BATTERY)
                             ? DISPLAY_COLOR_BATTERY_LOW_FILL
                             : DISPLAY_COLOR_MOD_ACTIVE;
        // Unknown level: show a full bar so the connection colour stays visible.
        int width = state.level >= 0 ? BAR_WIDTH * state.level / 100 : BAR_WIDTH;

        lv_obj_set_width(widget->fill, MAX(width, BAR_HEIGHT));
        lv_obj_set_style_bg_color(widget->fill, lv_color_hex(color), LV_PART_MAIN);

        if (state.connected && state.level >= 0) {
            lv_label_set_text_fmt(widget->label, "%d%%", state.level);
        } else {
            lv_label_set_text(widget->label, "--");
        }
        lv_obj_set_style_text_color(widget->label, lv_color_hex(color), LV_PART_MAIN);
    }
}

static struct split_indicator_state split_indicator_get_state(const zmk_event_t *eh) {
    if (eh != NULL) {
        const struct zmk_split_central_status_changed *status =
            as_zmk_split_central_status_changed(eh);
        const struct zmk_peripheral_battery_state_changed *battery =
            as_zmk_peripheral_battery_state_changed(eh);

        if (status != NULL && status->slot == 0) {
            keyboard_connected = status->connected;
        }
        if (battery != NULL && battery->source == 0) {
            battery_level = battery->state_of_charge;
        }
    }
    return (struct split_indicator_state){
        .connected = keyboard_connected,
        .level = battery_level,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_split_indicator, struct split_indicator_state,
                            split_indicator_update_cb, split_indicator_get_state)
ZMK_SUBSCRIPTION(widget_split_indicator, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(widget_split_indicator, zmk_peripheral_battery_state_changed);

static lv_obj_t *create_bar(lv_obj_t *parent, int width, uint32_t color) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, width, BAR_HEIGHT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    return bar;
}

int zmk_widget_split_indicator_init(struct zmk_widget_split_indicator *widget, lv_obj_t *parent) {
    // 210px bar + percentage label to its right, 260px wide in total.
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 260, 20);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    widget->track = create_bar(widget->obj, BAR_WIDTH, DISPLAY_COLOR_BATTERY_BG);
    widget->fill = create_bar(widget->obj, BAR_WIDTH, DISPLAY_COLOR_MOD_INACTIVE);

    widget->label = lv_label_create(widget->obj);
    lv_label_set_text(widget->label, "--");
    lv_obj_set_style_text_font(widget->label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(DISPLAY_COLOR_MOD_INACTIVE),
                                LV_PART_MAIN);
    lv_obj_align(widget->label, LV_ALIGN_RIGHT_MID, 0, 0);

    sys_slist_append(&widgets, &widget->node);
    widget_split_indicator_init();

    return 0;
}

lv_obj_t *zmk_widget_split_indicator_obj(struct zmk_widget_split_indicator *widget) {
    return widget->obj;
}
